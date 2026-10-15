"""Exercise individually shaped fixtures without using the core split code."""

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


BIN = Path(__file__).resolve().parents[3] / "bin"
GENERATOR = Path(os.environ.get("TEMP_GEN", BIN / "temp_gen")).resolve()
VERIFIER = Path(os.environ.get("TEMP_VERIFY", BIN / "temp_verify")).resolve()
NODE_SIZE = 128
MAX_KEY = (1 << 62) - 1


class Node:
    def __init__(self, level, children=None, keys=None):
        self.level = level
        self.children = children or []
        self.keys = keys or []

    @property
    def count(self):
        return len(self.children) - 1 if self.level else len(self.keys)

    @property
    def first(self):
        return self.children[0].first if self.level else self.keys[0]


class Image:
    """Small, independent encoder and decoder for the documented disk layout."""

    def __init__(self, normal):
        self.normal = normal
        self.pointer = "Q" if normal else "I"
        self.pointer_size = 8 if normal else 4
        self.leaf_max = 4
        self.children_max = 4 if normal else 6
        self.branch_max = self.children_max - 1
        self.branch_min = (self.children_max + 1) // 2 - 1

    def tree(self, height, overrides, start=0, interval=2):
        cursor = start

        def build(level, path):
            nonlocal cursor
            count = overrides.get(path, self.branch_max if level else self.leaf_max)
            if level:
                return Node(level, [build(level - 1, (*path, child))
                                    for child in range(count + 1)])
            keys = [cursor + index * interval for index in range(count)]
            cursor += count * interval
            return Node(0, keys=keys)

        return build(height - 1, ())

    @staticmethod
    def flatten(root):
        nodes = []

        def visit(node, parent=0, path=()):
            index = len(nodes) + 1
            nodes.append((node, parent, path))
            for child, descendant in enumerate(node.children):
                visit(descendant, index, (*path, child))

        visit(root)
        return nodes

    def totals(self, root):
        nodes = self.flatten(root)
        leaves = [node for node, _, _ in nodes if not node.level]
        return root.level + 1, len(nodes), len(leaves), sum(len(n.keys) for n in leaves)

    def encode(self, root):
        nodes = self.flatten(root)
        indices = {id(node): index for index, (node, _, _) in enumerate(nodes, 1)}
        layers = {}
        for index, (node, _, _) in enumerate(nodes, 1):
            layers.setdefault(node.level, []).append(index)
        height, count, _, records = self.totals(root)
        header = bytearray(NODE_SIZE)
        header[:4] = b"BPTR"
        struct.pack_into("=IIHHQI", header, 4, 0 if self.normal else 0x80,
                         NODE_SIZE, 8, 8, records, height)
        struct.pack_into("=" + self.pointer * 4, header, 28, 1, 0, 0, count)
        blocks = [header]
        for index, (node, parent, _) in enumerate(nodes, 1):
            layer = layers[node.level]
            position = layer.index(index)
            previous = layer[position - 1] if position else 0
            following = layer[position + 1] if position + 1 < len(layer) else 0
            keys = [child.first for child in node.children[1:]] if node.level else node.keys
            values = ([indices[id(child)] for child in node.children] if node.level
                      else [key * 2 for key in node.keys])
            block = bytearray(NODE_SIZE)
            struct.pack_into("=HHII", block, 0, 1 if node.level else 3,
                             node.level, len(keys), 0)
            struct.pack_into("=" + self.pointer * 3, block, 12,
                             parent, following, previous)
            struct.pack_into("=" + "q" * len(keys), block, 64, *keys)
            value_type = self.pointer if node.level else "q"
            struct.pack_into("=" + value_type * len(values), block,
                             64 + len(keys) * 8, *values)
            blocks.append(block)
        return bytearray(b"".join(blocks))

    def decode(self, data):
        root, free_head, free_count, count = struct.unpack_from(
            "=" + self.pointer * 4, data, 28)
        result = {"records": struct.unpack_from("=Q", data, 16)[0],
                  "height": struct.unpack_from("=I", data, 24)[0],
                  "nodes": count, "free": (free_head, free_count), "paths": {}}

        def visit(index, path=()):
            offset = index * NODE_SIZE
            flags, level, size = struct.unpack_from("=HHI", data, offset)
            parent, following, previous = struct.unpack_from(
                "=" + self.pointer * 3, data, offset + 12)
            keys = list(struct.unpack_from("=" + "q" * size, data, offset + 64))
            value_type = self.pointer if level else "q"
            value_count = size + 1 if level else size
            values = list(struct.unpack_from("=" + value_type * value_count,
                                            data, offset + 64 + size * 8))
            result["paths"][path] = {"index": index, "flags": flags,
                "level": level, "count": size, "parent": parent,
                "next": following, "prev": previous, "keys": keys, "values": values}
            if level:
                for child, child_index in enumerate(values):
                    visit(child_index, (*path, child))

        visit(root)
        return result

    def split_absorbed_by_parent(self, height):
        # At height 2 the root absorbs a leaf split. At height 4, child 0
        # absorbs a split of its first branch after that branch splits a leaf.
        overrides = {(): self.branch_max - 1} if height == 2 else {
            (0,): self.branch_max - 1}
        root = self.tree(height, overrides)
        parent = root if height == 2 else root.children[0].children[0]
        first = parent.children[0]
        records = [-1, *first.keys]
        parent.children[:1] = [Node(0, keys=records[:3]), Node(0, keys=records[3:])]
        if height == 4:
            grandparent = root.children[0]
            boundary = (len(parent.children) + 1) // 2
            grandparent.children[:1] = [Node(1, parent.children[:boundary]),
                                       Node(1, parent.children[boundary:])]
        return root, overrides


def selector(path):
    return ".".join(map(str, path)) if path else "root"


def shape_name(height, start, interval, overrides):
    suffix = ",".join(f"{selector(path)}={count}"
                      for path, count in sorted(overrides.items()))
    return f"{height}-{start}-{interval}" + (f"--keys-{suffix}" if suffix else "") + ".bptr"


class Shapes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="temp-shapes-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.serial = 0

    def generate(self, normal=False, height=3, overrides=None, start=0, interval=2,
                 options=None, directory=None):
        if directory is None:
            self.serial += 1
            directory = self.root / str(self.serial) / "templates"
        args = [str(GENERATOR), "--dir", str(directory), "--node-size", str(NODE_SIZE)]
        if normal:
            args += ["--norm"]
        if options is None:
            options = [f"{selector(path)}={count}"
                       for path, count in (overrides or {}).items()]
        for option in options:
            args += ["--node-keys", option]
        result = subprocess.run([*args, str(height), str(start), str(interval)],
                                capture_output=True, text=True, timeout=5)
        return result, directory

    def verify(self, path, overrides=None, scalars=False, height=3, start=0,
               interval=2, inserted=False, expected=None, extra=()):
        args = [str(VERIFIER)]
        if scalars:
            args += ["--lay-cnt", str(height), "--st", str(start),
                     "--interval", str(interval)]
        if overrides is not None:
            for node_path, count in overrides.items():
                args += ["--node-keys", f"{selector(node_path)}={count}"]
        if inserted:
            args += ["--key", "-1", "--val", "-2"]
        if expected is not None:
            for flag, count in zip(("height", "nodes", "leaves"), expected):
                args += [f"--expect-{flag}", str(count)]
        return subprocess.run([*args, *extra, str(path)], capture_output=True,
                              text=True, timeout=5)

    def assert_ok(self, result):
        self.assertEqual(result.returncode, 0, result)

    def assert_bad(self, result, code=1):
        self.assertEqual(result.returncode, code, result)
        self.assertNotIn(": ok", result.stdout)
        self.assertNotIn("reused", result.stdout)

    def assert_pristine(self, path, normal, height, overrides, start=0, interval=2):
        image = Image(normal)
        expected_tree = image.tree(height, overrides, start, interval)
        expected = image.decode(image.encode(expected_tree))
        data = path.read_bytes()
        actual = image.decode(data)
        self.assertEqual(actual["height"], height)
        self.assertEqual(actual["nodes"], expected["nodes"])
        self.assertEqual(actual["records"], expected["records"])
        self.assertEqual(actual["free"], (0, 0))
        self.assertEqual(len(data), (actual["nodes"] + 1) * NODE_SIZE)
        self.assertEqual(set(actual["paths"]), set(expected["paths"]))
        layers = {}
        for node_path, node in actual["paths"].items():
            target = expected["paths"][node_path]
            self.assertEqual(node["count"], target["count"], node_path)
            self.assertEqual(node["keys"], target["keys"], node_path)
            self.assertEqual(node["flags"], target["flags"], node_path)
            self.assertEqual(node["level"], target["level"], node_path)
            parent = actual["paths"][node_path[:-1]]["index"] if node_path else 0
            self.assertEqual(node["parent"], parent, node_path)
            if node["level"]:
                self.assertEqual(node["values"], [actual["paths"][(*node_path, child)]["index"]
                                  for child in range(node["count"] + 1)])
            else:
                self.assertEqual(node["values"], target["values"], node_path)
            layers.setdefault(node["level"], []).append((node_path, node))
        for layer in layers.values():
            layer.sort()
            for position, (_, node) in enumerate(layer):
                self.assertEqual(node["prev"], layer[position - 1][1]["index"] if position else 0)
                self.assertEqual(node["next"], layer[position + 1][1]["index"]
                                 if position + 1 < len(layer) else 0)

    def test_asymmetric_shapes_both_layouts(self):
        for normal in (False, True):
            image = Image(normal)
            overrides = {(): 1, (0,): image.branch_min, (0, 0): 2, (1, 1): 3}
            with self.subTest(normal=normal):
                result, directory = self.generate(normal, overrides=overrides, start=-7, interval=5)
                self.assert_ok(result)
                path = directory / shape_name(3, -7, 5, overrides)
                self.assert_pristine(path, normal, 3, overrides, -7, 5)
                self.assert_ok(self.verify(path))

    def test_root_exceptions_and_explicit_full_counts(self):
        for normal in (False, True):
            image = Image(normal)
            cases = [(1, {(): 1}), (1, {(): 4}), (2, {(): 1}),
                     (3, {(): image.branch_max, (0,): image.branch_min,
                          (0, 0): 2, (0, 1): 4})]
            for height, overrides in cases:
                with self.subTest(normal=normal, height=height, overrides=overrides):
                    result, directory = self.generate(normal, height, overrides)
                    self.assert_ok(result)
                    path = directory / shape_name(height, 0, 2, overrides)
                    self.assert_pristine(path, normal, height, overrides)
                    self.assert_ok(self.verify(path))

    def test_order_and_decimal_spelling_canonicalize_and_reuse(self):
        for normal in (False, True):
            image = Image(normal)
            overrides = {(): 1, (0,): image.branch_min, (0, 1): 2, (1, 0): 3}
            options = ["01.00=003", "000.01=002", f"00={image.branch_min:03}", "root=001"]
            with self.subTest(normal=normal):
                first, directory = self.generate(normal, overrides=overrides, options=options)
                self.assert_ok(first)
                path = directory / shape_name(3, 0, 2, overrides)
                before = path.read_bytes()
                second, _ = self.generate(normal, overrides=overrides, directory=directory)
                self.assert_ok(second)
                self.assertIn("reused", second.stdout)
                self.assertEqual(path.read_bytes(), before)
                self.assertEqual(list(directory.iterdir()), [path])

    def test_numeric_child_order_in_names(self):
        overrides = {(10,): 3, (2,): 2, (0,): 4}
        directory = self.root / "numeric"
        # 512-byte nodes permit child 10. Numeric sorting must precede textual
        # sorting, which would incorrectly put child 10 before child 2.
        args = [str(GENERATOR), "--dir", str(directory), "--node-size", "512"]
        for path, count in overrides.items():
            # Nonroot leaves in this layout need at least 14 records.
            args += ["--node-keys", f"{selector(path)}={count + 14}"]
        result = subprocess.run([*args, "2", "0", "2"], capture_output=True,
                                text=True, timeout=5)
        self.assert_ok(result)
        path = directory / "2-0-2--keys-0=18,2=16,10=17.bptr"
        self.assertTrue(path.is_file())
        self.assert_ok(self.verify(path))

    def test_full_and_custom_shapes_coexist(self):
        for normal in (False, True):
            directory = self.root / str(normal)
            for overrides in ({}, {(): 1}, {(): 2}):
                with self.subTest(normal=normal, overrides=overrides):
                    result, _ = self.generate(normal, 2, overrides, directory=directory)
                    self.assert_ok(result)
                    path = directory / shape_name(2, 0, 2, overrides)
                    self.assert_pristine(path, normal, 2, overrides)
                    self.assert_ok(self.verify(path))
            self.assertEqual(len(list(directory.iterdir())), 3)

    def test_infers_shape_even_with_all_scalar_flags(self):
        for normal in (False, True):
            overrides = {(): 1, (0,): 2}
            result, directory = self.generate(normal, 2, overrides)
            self.assert_ok(result)
            path = directory / shape_name(2, 0, 2, overrides)
            with self.subTest(normal=normal):
                self.assert_ok(self.verify(path, scalars=True, height=2))

    def test_explicit_override_set_replaces_filename_set(self):
        for normal in (False, True):
            image = Image(normal)
            named = {(): 1, (0, 0): 2}
            actual = {(0,): image.branch_min}
            path = self.root / shape_name(3, 0, 2, named)
            path.write_bytes(image.encode(image.tree(3, actual)))
            with self.subTest(normal=normal):
                self.assert_ok(self.verify(path, overrides=actual))
                self.assert_bad(self.verify(path))

    def test_invalid_selector_syntax_and_duplicate_canonical_paths(self):
        invalid = ["", "root", "root=", "=2", "Root=2", "root.0=2", ".0=2",
                   "0.=2", "0..1=2", "-1=2", "+0=2", " 0=2", "0 =2",
                   "0=-1", "0=+2", "0= 2", "0=2x", "0=2=3", "0=4294967296",
                   "4294967296=2"]
        for option in invalid:
            with self.subTest(option=option):
                result, directory = self.generate(options=[option])
                self.assert_bad(result, 2)
                self.assertFalse(directory.parent.exists())
                self.assert_bad(self.verify(self.root / "missing.bptr", scalars=True,
                                           extra=("--node-keys", option)), 2)
        for options in (["0=2", "00=3"], ["root=1", "root=2"],
                        ["0.01=2", "00.1=3"]):
            with self.subTest(options=options):
                result, directory = self.generate(options=options)
                self.assert_bad(result, 2)
                self.assertFalse(directory.parent.exists())
                flags = [value for option in options for value in ("--node-keys", option)]
                self.assert_bad(self.verify(self.root / "missing.bptr", scalars=True,
                                           extra=flags), 2)

    def test_invalid_shape_semantics_before_directories(self):
        for normal in (False, True):
            image = Image(normal)
            invalid = [(1, {(): 0}), (1, {(): 5}), (2, {(): 0}),
                       (2, {(): image.branch_max + 1}), (2, {(0,): 1}),
                       (2, {(0,): 5}), (3, {(0,): image.branch_min - 1}),
                       (3, {(0,): image.branch_max + 1}),
                       (2, {(image.children_max,): 2}), (2, {(0, 0): 2}),
                       (3, {(0, image.branch_min + 1): 2, (0,): image.branch_min}),
                       (3, {(2, 0): 2, (): 1})]
            for height, overrides in invalid:
                with self.subTest(normal=normal, height=height, overrides=overrides):
                    result, directory = self.generate(normal, height, overrides)
                    self.assert_bad(result)
                    self.assertFalse(directory.parent.exists())

    def test_deep_selectors_and_overlong_names_fail_before_directories(self):
        result, directory = self.generate(height=254, options=[".".join(["0"] * 254) + "=2"])
        self.assertIn(result.returncode, (1, 2), result)
        self.assertFalse(directory.parent.exists())
        overrides = {(a, b, c): 2 for a in range(4) for b in range(4) for c in range(4)}
        result, directory = self.generate(True, 4, overrides)
        self.assert_bad(result)
        self.assertFalse(directory.parent.exists())

    def test_custom_shape_controls_overflow_and_negative_interval_preflight(self):
        overrides = {(): 1, (0,): 2, (1,): 3}
        for normal in (False, True):
            with self.subTest(normal=normal):
                result, directory = self.generate(normal, 2, overrides, start=MAX_KEY - 4, interval=1)
                self.assert_ok(result)
                path = directory / shape_name(2, MAX_KEY - 4, 1, overrides)
                self.assert_pristine(path, normal, 2, overrides, MAX_KEY - 4, 1)
                self.assert_ok(self.verify(path))
                result, directory = self.generate(normal, 2, overrides, start=MAX_KEY - 3, interval=1)
                self.assert_bad(result)
                self.assertIn("key/value overflow", result.stderr)
                self.assertFalse(directory.parent.exists())
                result, directory = self.generate(normal, 2, overrides, start=100, interval=-2)
                self.assert_bad(result)
                self.assertIn("retry with interval 2 and st 92", result.stderr)
                self.assertFalse(directory.parent.exists())

    def test_sparse_huge_shapes_fail_preflight_promptly(self):
        for normal in (False, True):
            for start, interval in ((0, 0), (0, 1), (MAX_KEY, 1)):
                with self.subTest(normal=normal, start=start, interval=interval):
                    result, directory = self.generate(normal, 254, {(): 1}, start, interval)
                    self.assert_bad(result)
                    self.assertFalse(directory.parent.exists())

    def test_custom_reuse_checks_layout_and_preserves_file(self):
        overrides = {(): 1, (0,): 2}
        result, directory = self.generate(False, 2, overrides)
        self.assert_ok(result)
        path = directory / shape_name(2, 0, 2, overrides)
        before = path.read_bytes()
        result, _ = self.generate(True, 2, overrides, directory=directory)
        self.assert_bad(result)
        self.assertEqual(path.read_bytes(), before)

    def test_corrupted_custom_images_are_never_reused_or_modified(self):
        for normal in (False, True):
            image = Image(normal)
            overrides = {(): 1, (0, 0): 2, (0, 1): 3}
            original = image.encode(image.tree(3, overrides))
            decoded = image.decode(original)
            leaf_index = decoded["paths"][(0, 0)]["index"]
            branch_index = decoded["paths"][(0,)]["index"]
            mutations = {
                "record-total": (16, "Q", decoded["records"] + 1),
                "node-total": (28 + 3 * image.pointer_size, image.pointer, decoded["nodes"] - 1),
                "no-allocated-nodes": (28 + 3 * image.pointer_size, image.pointer, 0),
                "root-occupancy": (NODE_SIZE + 4, "I", 2),
                "branch-occupancy": (branch_index * NODE_SIZE + 4, "I", image.branch_max - 1),
                "leaf-occupancy": (leaf_index * NODE_SIZE + 4, "I", 3),
                "free-node": (28 + 2 * image.pointer_size, image.pointer, 1),
            }
            for label, mutation in [*mutations.items(), ("truncated", None), ("appended", None)]:
                with self.subTest(normal=normal, mutation=label):
                    data = bytearray(original)
                    if mutation:
                        offset, kind, value = mutation
                        struct.pack_into("=" + kind, data, offset, value)
                    elif label == "truncated":
                        data = data[:-1]
                    else:
                        data += bytes(NODE_SIZE)
                    directory = self.root / f"{normal}-{label}"
                    directory.mkdir()
                    path = directory / shape_name(3, 0, 2, overrides)
                    path.write_bytes(data)
                    result, _ = self.generate(normal, 3, overrides, directory=directory)
                    self.assert_bad(result)
                    self.assertEqual(path.read_bytes(), data)

    def test_exact_original_occupancy_not_only_totals(self):
        for normal in (False, True):
            image = Image(normal)
            overrides = {(): 1, (0,): 2, (1,): 3}
            data = image.encode(image.tree(2, {(): 1, (0,): 3, (1,): 2}))
            directory = self.root / f"swapped-{normal}"
            directory.mkdir()
            path = directory / shape_name(2, 0, 2, overrides)
            path.write_bytes(data)
            with self.subTest(normal=normal):
                self.assert_bad(self.verify(path))
                result, _ = self.generate(normal, 2, overrides, directory=directory)
                self.assert_bad(result)
                self.assertEqual(path.read_bytes(), data)

    def inserted_image(self, normal, height):
        image = Image(normal)
        tree, overrides = image.split_absorbed_by_parent(height)
        path = self.root / shape_name(height, 0, 2, overrides)
        data = image.encode(tree)
        path.write_bytes(data)
        return image, path, data, image.totals(tree)[:3]

    def test_independent_splits_stop_at_existing_ancestor(self):
        for normal in (False, True):
            for height in (2, 4):
                with self.subTest(normal=normal, height=height):
                    image, path, _, expected = self.inserted_image(normal, height)
                    self.assert_ok(self.verify(path, inserted=True, expected=expected))
                    for position in range(3):
                        incorrect = list(expected)
                        incorrect[position] += 1
                        self.assert_bad(self.verify(path, inserted=True, expected=incorrect))

    def test_custom_insert_requires_complete_expected_result_flags(self):
        image, path, _, expected = self.inserted_image(False, 2)
        for flags in ((), ("--expect-height", "2"), ("--expect-nodes", str(expected[1])),
                      ("--expect-leaves", str(expected[2])),
                      ("--expect-height", "2", "--expect-nodes", str(expected[1]))):
            with self.subTest(flags=flags):
                self.assert_bad(self.verify(path, inserted=True, extra=flags), 2)
        self.assert_bad(self.verify(path, expected=expected), 2)
        self.assert_bad(self.verify(path, extra=("--expect-height", "2")), 2)
        for option in ("--expect-height", "--expect-nodes", "--expect-leaves"):
            for invalid in ("-1", "+1", " 1", "1x", "18446744073709551616"):
                with self.subTest(option=option, invalid=invalid):
                    self.assert_bad(self.verify(path, inserted=True,
                                               extra=(option, invalid)), 2)

    def test_inserted_record_accounting_uses_original_custom_shape(self):
        for normal in (False, True):
            image, path, data, expected = self.inserted_image(normal, 2)
            correct_records = struct.unpack_from("=Q", data, 16)[0]
            with self.subTest(normal=normal):
                # A header that agrees with a full original shape is still
                # wrong for this custom shape's original records plus one.
                for records in (correct_records - 1, image.children_max * image.leaf_max + 1):
                    forged = bytearray(data)
                    struct.pack_into("=Q", forged, 16, records)
                    path.write_bytes(forged)
                    self.assert_bad(self.verify(path, inserted=True, expected=expected))

    def test_custom_insert_without_split_and_reuse_rejection(self):
        for normal in (False, True):
            image = Image(normal)
            overrides = {(): 1}
            directory = self.root / f"in-place-{normal}"
            directory.mkdir()
            path = directory / shape_name(1, 0, 2, overrides)
            # A legitimate insertion into a non-full root allocates no nodes
            # and changes neither height nor file size. It is a valid instance
            # but cannot be reused as its pristine one-record template.
            data = image.encode(Node(0, keys=[-1, 0]))
            path.write_bytes(data)
            with self.subTest(normal=normal):
                self.assert_ok(self.verify(path, inserted=True, expected=(1, 1, 1)))
                before = path.read_bytes()
                result, _ = self.generate(normal, 1, overrides, directory=directory)
                self.assert_bad(result)
                self.assertEqual(path.read_bytes(), before)

    def test_semantically_invalid_verifier_shape_leaves_image_untouched(self):
        for normal in (False, True):
            image = Image(normal)
            path = self.root / f"semantic-{normal}.bptr"
            original = image.encode(image.tree(2, {(): 1}))
            for overrides in ({(): 0}, {(0,): 1}, {(0, 0): 2}, {(): 1, (2,): 2}):
                with self.subTest(normal=normal, overrides=overrides):
                    path.write_bytes(original)
                    self.assert_bad(self.verify(path, overrides, scalars=True, height=2), 2)
                    self.assertEqual(path.read_bytes(), original)

    def test_zero_level_filename_is_not_a_valid_request(self):
        path = self.root / "0-0-2.bptr"
        data = Image(False).encode(Node(0, keys=[0, 2, 4, 6]))
        path.write_bytes(data)
        self.assert_bad(self.verify(path), 2)
        self.assertEqual(path.read_bytes(), data)

    def test_inserted_actual_traversal_must_match_declared_node_total(self):
        for normal in (False, True):
            image, path, data, expected = self.inserted_image(normal, 4)
            forged = bytearray(data)
            forged += bytes(NODE_SIZE)
            struct.pack_into("=" + image.pointer, forged,
                             28 + 3 * image.pointer_size, expected[1] + 1)
            path.write_bytes(forged)
            with self.subTest(normal=normal):
                # Both the declared expectation and header include an orphan
                # block. Only counting the real traversal detects the defect.
                self.assert_bad(self.verify(path, inserted=True,
                    expected=(expected[0], expected[1] + 1, expected[2])))

    def test_inserted_images_retain_structure_checks(self):
        for normal in (False, True):
            image, path, data, expected = self.inserted_image(normal, 4)
            decoded = image.decode(data)
            first_leaf = decoded["paths"][(0, 0, 0)]["index"]
            second_branch = decoded["paths"][(0, 1)]["index"]
            mutations = [(first_leaf * NODE_SIZE, "H", 1),
                         (first_leaf * NODE_SIZE + 12, image.pointer, 0),
                         (first_leaf * NODE_SIZE + 12 + image.pointer_size, image.pointer, 0),
                         (second_branch * NODE_SIZE + 12, image.pointer, 0),
                         (NODE_SIZE + 64, "q", -123)]
            for offset, kind, value in mutations:
                with self.subTest(normal=normal, offset=offset):
                    forged = bytearray(data)
                    struct.pack_into("=" + kind, forged, offset, value)
                    path.write_bytes(forged)
                    self.assert_bad(self.verify(path, inserted=True, expected=expected))


if __name__ == "__main__":
    unittest.main()
