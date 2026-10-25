"""Independent image encodings and external modules exercise the new oracle."""
import copy
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
GEN = Path(os.environ.get("MANIFEST_GEN", ROOT / "bin/temp_gen"))
VERIFY = Path(os.environ.get("MANIFEST_VERIFY", ROOT / "bin/temp_verify"))
PLUGIN = ROOT / "obj/utils/temp/comparator_plugin.so"
SOURCE = ROOT / "utils/temp/src"


def spec(records=(), lite=True, key_size=8, value_size=8, profile="i64-asc"):
    return {"schema": 1,
            "layout": {"lite": lite, "node_size": 128,
                       "key_size": key_size, "value_size": value_size},
            "comparator": {"profile": profile},
            "records": [{"key_hex": key.hex(), "value_hex": val.hex()}
                        for key, val in records]}


def pairs(count, start=0):
    return [(struct.pack("=q", start + i), struct.pack("=q", 991 - 13 * i))
            for i in range(count)]


class Node:
    def __init__(self, records=None, children=None):
        self.records = records
        self.children = children

    @property
    def first(self):
        return self.children[0].first if self.children else self.records[0][0]


def encode(document, root, free=0):
    """Write the documented format directly; never call utility generation."""
    layout = document["layout"]
    pointer = "I" if layout["lite"] else "Q"
    width = 4 if layout["lite"] else 8
    nodes, layers = [], {}

    def walk(node, parent=0):
        index = len(nodes) + 1
        nodes.append([node, parent, None])
        level = 0
        if node.children:
            child_levels = [walk(child, index)[1] for child in node.children]
            assert len(set(child_levels)) == 1
            level = child_levels[0] + 1
        nodes[index - 1][2] = level
        layers.setdefault(level, []).append(index)
        return index, level

    height = walk(root)[1] + 1 if root else 0
    indices = {id(node): i + 1 for i, (node, _, _) in enumerate(nodes)}
    header = bytearray(128)
    header[:4] = b"BPTR"
    count = sum(len(n.records) for n, _, _ in nodes if n.records is not None)
    struct.pack_into("=IIHHQI", header, 4, 0x80 if layout["lite"] else 0,
                     128, layout["key_size"], layout["value_size"], count, height)
    struct.pack_into("=" + pointer * 4, header, 28,
                     1 if root else 0, len(nodes) + 1 if free else 0, free, len(nodes))
    blocks = [header]
    for index, (node, parent, level) in enumerate(nodes, 1):
        block = bytearray(128)
        keys = [child.first for child in node.children[1:]] if node.children else [k for k, _ in node.records]
        values = ([struct.pack("=" + pointer, indices[id(child)]) for child in node.children]
                  if node.children else [v for _, v in node.records])
        layer = layers[level]
        at = layer.index(index)
        struct.pack_into("=HHII", block, 0, 1 if level else 3, level, len(keys), 0)
        struct.pack_into("=" + pointer * 3, block, 12, parent,
                         layer[at + 1] if at + 1 < len(layer) else 0,
                         layer[at - 1] if at else 0)
        payload = b"".join(keys + values)
        assert len(payload) <= 64
        block[64:64 + len(payload)] = payload
        blocks.append(block)
    for i in range(free):
        block = bytearray(b"\xa5" * 128)
        struct.pack_into("=H", block, 0, 0xfffe)  # only valid bit matters
        struct.pack_into("=" + pointer, block, 2, len(nodes) + i + 2 if i + 1 < free else 0)
        blocks.append(block)
    return bytearray(b"".join(blocks))


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="bptr manifest ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)

    def save(self, document, name="spec.json"):
        path = self.work / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def run_tool(self, command, status=0, timeout=10):
        result = subprocess.run([str(x) for x in command], capture_output=True,
                                text=True, timeout=timeout)
        self.assertEqual(result.returncode, status, result)
        out = json.loads(result.stdout)
        self.assertEqual(out["status"], status)
        return out

    def verify(self, document, data, status=0, extra=()):
        path = self.work / "image.bptr"
        path.write_bytes(data)
        path.chmod(0o444)
        before = hashlib.sha256(path.read_bytes()).digest()
        result = self.run_tool([VERIFY, "--json", "--expect", self.save(document), path, *extra], status)
        self.assertEqual(hashlib.sha256(path.read_bytes()).digest(), before)
        path.chmod(0o644)
        return result

    def generate(self, document, status=0, extra=(), name="generated/image.bptr"):
        path = self.work / name
        result = self.run_tool([GEN, "--json", "--spec", self.save(document), "--out", path, *extra], status)
        return path, result

    def test_empty_singleton_and_arbitrary_bytes(self):
        for lite in (False, True):
            for count in (0, 1, 2):
                with self.subTest(lite=lite, count=count):
                    records = [(i.to_bytes(3, "big"), bytes([0, 255 - i])) for i in range(count)]
                    document = spec(records, lite, 3, 2, "bytes-asc")
                    document["initial_shape"] = {"height": int(count != 0), "node_keys": {"root": count} if count else {}}
                    root = Node(records) if count else None
                    self.verify(document, encode(document, root), extra=("--check", "contents,structure,shape"))
                    path, report = self.generate(document)
                    self.assertFalse(report["reused"])
                    self.assertEqual(path.read_bytes(), encode(document, root))
                    _, report = self.generate(document)
                    self.assertTrue(report["reused"])
                    path.unlink()

    def test_multiple_legal_distributions(self):
        for lite in (False, True):
            document = spec(pairs(6), lite)
            for left in (2, 3, 4):
                with self.subTest(lite=lite, left=left):
                    root = Node(children=[Node(pairs(6)[:left]), Node(pairs(6)[left:])])
                    report = self.verify(document, encode(document, root))
                    self.assertEqual((report["height"], report["nodes"], report["leaves"]), (2, "3", "2"))

    def test_generated_asymmetric_trees_match_independent_encoder(self):
        for lite in (False, True):
            for key_size, value_size in ((1, 1), (3, 2), (7, 7), (9, 37)):
                with self.subTest(lite=lite, key_size=key_size, value_size=value_size):
                    pointer = 4 if lite else 8
                    branch = (64 - pointer) // (key_size + pointer)
                    leaf = 64 // (key_size + value_size)
                    branch_keys = branch // 2
                    leaf_keys = (leaf + 1) // 2
                    overrides = {"root": 1, "0": branch_keys, "1": branch_keys}
                    children, records = [], []
                    for a in range(2):
                        leaves = []
                        for b in range(branch_keys + 1):
                            count = leaf_keys if key_size == 1 or b % 2 == 0 else leaf
                            overrides[f"{a}.{b}"] = count
                            group = []
                            for _ in range(count):
                                i = len(records)
                                pair = (i.to_bytes(key_size, "big"), bytes([(17 * i) % 256]) * value_size)
                                records.append(pair); group.append(pair)
                            leaves.append(Node(group))
                        children.append(Node(children=leaves))
                    document = spec(records, lite, key_size, value_size, "bytes-asc")
                    document["initial_shape"] = {"height": 3, "node_keys": overrides}
                    path, _ = self.generate(document)
                    self.assertEqual(path.read_bytes(), encode(document, Node(children=children)))
                    self.verify(document, path.read_bytes(), extra=("--check", "contents,structure,shape"))
                    path.unlink()

    def test_exact_shape_is_optional(self):
        document = spec(pairs(6))
        document["initial_shape"] = {"height": 2, "node_keys": {"root": 1, "0": 2}}
        root = Node(children=[Node(pairs(6)[:3]), Node(pairs(6)[3:])])
        self.verify(document, encode(document, root))
        self.verify(document, encode(document, root), 1, ("--check", "contents,structure,shape"))

    def test_numeric_shape_paths_and_override_order(self):
        # Child 10 sorts after child 2 numerically. Keep the root override
        # between them in input order and verify against a separate encoding.
        overrides = {"10": 17, "2": 16, "root": 10}
        overrides.update({str(i): 16 for i in range(10)})
        records, children = [], []
        for child in range(11):
            group = []
            for _ in range(overrides[str(child)]):
                i = len(records)
                pair = (bytes([i]), bytes([(99 - i) % 256]))
                group.append(pair); records.append(pair)
            children.append(Node(group))
        document = spec(records, key_size=1, value_size=1, profile="bytes-asc")
        document["initial_shape"] = {"height": 2, "node_keys": overrides}
        expected = encode(document, Node(children=children))
        for node_keys in (overrides, dict(reversed(list(overrides.items())))):
            document["initial_shape"]["node_keys"] = node_keys
            path, _ = self.generate(document)
            self.assertEqual(path.read_bytes(), expected)
            self.verify(document, expected, extra=("--check", "contents,structure,shape"))
            path.unlink()

    def test_topology_constraints_independent(self):
        document = spec(pairs(6))
        root = Node(children=[Node(pairs(6)[:3]), Node(pairs(6)[3:])])
        for field, value in (("height", 2), ("nodes", 3), ("leaves", 2)):
            document["expect"] = {field: value}
            self.verify(document, encode(document, root))
            document["expect"][field] += 1
            self.verify(document, encode(document, root), 1)

    def test_wrong_missing_extra_duplicate_and_unordered_records(self):
        document = spec(pairs(6))
        mutations = [pairs(5), pairs(7), [*pairs(3), *pairs(3, 2)],
                     [*pairs(3), *reversed(pairs(3, 3))]]
        wrong = pairs(6)
        wrong[4] = (wrong[4][0], b"\xff" * 8)
        mutations.append(wrong)
        for records in mutations:
            with self.subTest(records=records):
                left = 3 if len(records) >= 6 else 2
                root = Node(children=[Node(records[:left]), Node(records[left:])])
                self.verify(document, encode(document, root), 1)

    def test_corrupt_structure_and_extents(self):
        for lite in (False, True):
            document = spec(pairs(6), lite)
            data = encode(document, Node(children=[Node(pairs(6)[:3]), Node(pairs(6)[3:])]))
            pointer = "I" if lite else "Q"
            width = 4 if lite else 8
            cases = []
            for offset, fmt, value in [
                (16, "Q", 99), (24, "I", 255), (28, pointer, 99),
                (128 + 4, "I", 0xffffffff), (128 + 72, pointer, 1),
                (128 + 72 + width, pointer, 2),
                (256 + 12, pointer, 3), (256 + 12 + width, pointer, 2),
                (384 + 12 + 2 * width, pointer, 0), (128 + 64, "q", 999),
                (128, "H", 3), (256, "H", 0)]:
                altered = bytearray(data)
                struct.pack_into("=" + fmt, altered, offset, value)
                cases.append(altered)
            cases.extend([data[:20], data[:-1], data + bytes(128)])
            for i, altered in enumerate(cases):
                with self.subTest(lite=lite, mutation=i):
                    self.verify(document, altered, 1)

    def test_free_list_accounting_and_cycles(self):
        for lite in (False, True):
            document = spec(pairs(1), lite)
            data = encode(document, Node(pairs(1)), free=2)
            self.verify(document, data)
            pointer, width = ("I", 4) if lite else ("Q", 8)
            for offset, value in ((256 + 2, 2), (256 + 2, 1), (28 + 2 * width, 3)):
                altered = bytearray(data)
                struct.pack_into("=" + pointer, altered, offset, value)
                self.verify(document, altered, 1)
            empty = spec([], lite)
            self.verify(empty, encode(empty, None, free=2))

    def test_descending_and_nonunit_comparators(self):
        for profile in ("i64-desc", "u64-desc", "bytes-desc", "bytes-nonunit"):
            records = pairs(4)
            if profile == "bytes-nonunit":
                records = sorted(records)
            else:
                records.reverse()
            document = spec(records, profile=profile)
            self.verify(document, encode(document, Node(records)))

    def test_equivalence_across_leaf_boundary(self):
        records = [(bytes([i, 0, 1]), b"\xff" * 8) for i in range(8)]
        document = spec(records, key_size=3, profile="prefix-asc")
        document["comparator"]["prefix_bytes"] = 1
        root = Node(children=[Node(records[:4]), Node(records[4:])])
        self.verify(document, encode(document, root))
        root.children[1].records = [(bytes([3, 9, 9]), b"\xff" * 8), *records[5:]]
        report = self.verify(document, encode(document, root), 1)
        self.assertEqual(report["invariant"], "record-order")

    def test_lattice_boundaries_and_independent_values(self):
        for start, step, value_start, value_step, count in [
            (-(1 << 63), 1, (1 << 63) - 1, -1, 3),
            ((1 << 63) - 1, -1, -(1 << 63), 1, 3),
            (-(1 << 63), (1 << 63) - 1, 0, -1, 3),
            ((1 << 63) - 1, -(1 << 63), 0, 1, 2)]:
            with self.subTest(start=start, step=step):
                records = [(struct.pack("=q", start + i * step), struct.pack("=q", value_start + i * value_step)) for i in range(count)]
                document = spec([], profile="i64-asc" if step > 0 else "i64-desc")
                document["initial_shape"] = {"height": 1, "node_keys": {"root": count}}
                document["records"] = {"kind": "i64-lattice", "count": count,
                    "start": str(start), "step": str(step),
                    "value_start": str(value_start), "value_step": str(value_step)}
                path, _ = self.generate(document)
                self.assertEqual(path.read_bytes(), encode(document, Node(records)))
                path.unlink()

    def test_generation_preflight_and_reuse_never_overwrite(self):
        document = spec(pairs(2))
        document["initial_shape"] = {"height": 1, "node_keys": {"root": 2}}
        path, _ = self.generate(document)
        original = path.read_bytes()
        document["records"][1]["value_hex"] = "ff" * 8
        self.generate(document, 1)
        self.assertEqual(path.read_bytes(), original)
        document["initial_shape"] = {"height": 254}
        self.generate(document, 2, name="absent/new/image.bptr")
        self.assertFalse((self.work / "absent").exists())

    def test_legacy_library_round_trip(self):
        # Public CLI integration only: use existing generation/loading without
        # inspecting or invoking bptr_insert. The independent driver still links
        # no core objects; these subprocesses check format interoperability.
        for lite in (False, True):
            directory = self.work / ("lite" if lite else "normal")
            command = [str(ROOT / "bin/temp_gen"), "--dir", str(directory), "--node-size", "128"]
            if not lite:
                command.append("--norm")
            command.extend(["2", "0", "16"])
            subprocess.run(command, check=True, capture_output=True)
            path = directory / "2-0-16.bptr"
            count = 24 if lite else 16
            records = [(struct.pack("=q", i * 16), struct.pack("=q", i * 32)) for i in range(count)]
            document = spec(records, lite)
            self.verify(document, path.read_bytes())
            document["initial_shape"] = {"height": 2}
            fresh, _ = self.generate(document, name=f"fresh-{lite}/2-0-16.bptr")
            subprocess.run([str(ROOT / "bin/temp_verify"), str(fresh)], check=True, capture_output=True)

    def test_external_comparator_and_override(self):
        module = self.work / "special order.so"
        shutil.copyfile(PLUGIN, module)
        records = [(struct.pack("=qQ", i, 99 - i), bytes([0, i, 255])) for i in range(2)]
        document = spec(records, key_size=16, value_size=3)
        document["comparator"] = {"library": module.name, "id": "test/id-only/v1"}
        document["initial_shape"] = {"height": 1, "node_keys": {"root": 2}}
        path, report = self.generate(document)
        self.assertEqual(report["comparator"], "test/id-only/v1")
        self.verify(document, encode(document, Node(records)))
        document["comparator"]["library"] = "missing.so"
        self.run_tool([VERIFY, "--json", "--expect", self.save(document), path], 2)
        self.run_tool([VERIFY, "--json", "--expect", self.save(document), path,
                       "--comparator-library", module])
        document["comparator"]["id"] = "wrong-id"
        self.run_tool([VERIFY, "--json", "--expect", self.save(document), path,
                       "--comparator-library", module], 2)

    def test_invalid_plugin_abi_and_missing_entry(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = shlex.split(os.environ.get("TEMP_PLUGIN_FLAGS", "-fPIC -dynamiclib" if sys.platform == "darwin" else "-fPIC -shared"))
        document = spec(pairs(1))
        document["comparator"] = {"library": "bad.so", "id": "bad"}
        source, module = self.work / "bad.c", self.work / "bad.so"
        for body in ["int unused(void) { return 0; }",
                     "const struct temp_comparator_descriptor_v1 *temp_comparator_v1(void) { return 0; }",
                     "const struct temp_comparator_descriptor_v1 *temp_comparator_v1(void) { static const struct temp_comparator_descriptor_v1 d = {99, sizeof d, \"bad\", 8, 0, 0}; return &d; }"]:
            source.write_text('#include "temp_compare.h"\n' + body)
            subprocess.run([*compiler, *flags, "-I", str(SOURCE), str(source), "-o", str(module)], check=True, capture_output=True)
            self.generate(document, 2)

    def test_invalid_manifests_and_json(self):
        document = spec(pairs(2))
        document["initial_shape"] = {"height": 1, "node_keys": {"root": 2}}
        variants = []
        for key, value in (("schema", 2), ("unknown", True), ("byte_order", "wrong")):
            changed = copy.deepcopy(document); changed[key] = value; variants.append(changed)
        for key, value in (("key_size", 0), ("value_size", 65536), ("node_size", 10), ("lite", 1)):
            changed = copy.deepcopy(document); changed["layout"][key] = value; variants.append(changed)
        for shape in ({"height": 0}, {"height": 1, "node_keys": {"0": 1}}, {"height": 2, "node_keys": {"root": 1, "2": 4}},
                      {"height": 2, "node_keys": {"00": 4}}):
            changed = copy.deepcopy(document); changed["initial_shape"] = shape; variants.append(changed)
        changed = copy.deepcopy(document); changed["records"].reverse(); variants.append(changed)
        changed = copy.deepcopy(document); changed["records"][1] = changed["records"][0]; variants.append(changed)
        changed = copy.deepcopy(document); changed["records"][0]["key_hex"] = "zz" * 8; variants.append(changed)
        for variant in variants:
            with self.subTest(variant=variant):
                self.generate(variant, 2)
                self.assertFalse((self.work / "generated").exists())
        path = self.work / "bad.json"
        for data in [b'{"schema":1,"schema":1}', b'{"x": [1,]}', b'{"x":01}', b'{"x":"\\u0000"}',
                     b'{"x":"\\ud800"}', b'{"x":"\xc0\x80"}', b'{}\0{}', b'[' * 66 + b']' * 66]:
            path.write_bytes(data)
            self.run_tool([GEN, "--json", "--spec", path, "--out", self.work / "bad.bptr"], 2)

    def test_manifest_size_boundary_and_unicode(self):
        document = spec(pairs(1))
        document["initial_shape"] = {"height": 1, "node_keys": {"root": 1}}
        path = self.work / "large.json"
        encoded = json.dumps(document).encode()
        padded = encoded + b" " * (16 * 1024 * 1024 - len(encoded))
        path.write_bytes(padded)
        self.run_tool([GEN, "--json", "--spec", path, "--out", self.work / "large.bptr"])
        path.write_bytes(padded + b" ")
        self.run_tool([GEN, "--json", "--spec", path, "--out", self.work / "large.bptr"], 2)
        # Exercise both raw UTF-8 and a surrogate pair through recognized ID
        # fields. A valid Unicode string must reach profile validation.
        for profile in ("\U0001f332", "é"):
            document["comparator"]["profile"] = profile
            for escaped in (False, True):
                path.write_text(json.dumps(document, ensure_ascii=escaped), encoding="utf-8")
                result = self.run_tool([GEN, "--json", "--spec", path, "--out", self.work / "unicode.bptr"], 2)
                self.assertIn("unknown comparator", result["detail"])

    def test_io_failures_and_seeded_mutations(self):
        document = spec(pairs(6))
        self.run_tool([VERIFY, "--json", "--expect", self.save(document), self.work / "missing.bptr"], 3)
        parent = self.work / "parent"
        parent.write_bytes(b"untouched")
        document["initial_shape"] = {"height": 2, "node_keys": {"root": 1, "0": 3, "1": 3}}
        self.generate(document, 3, name="parent/child.bptr")
        self.assertEqual(parent.read_bytes(), b"untouched")
        original = encode(document, Node(children=[Node(pairs(6)[:3]), Node(pairs(6)[3:])]))
        rng = random.Random(90210)
        image = self.work / "mutant.bptr"
        for _ in range(100):
            mutated = bytearray(original)
            index = rng.randrange(len(mutated))
            mutated[index] ^= 1 << rng.randrange(8)
            image.write_bytes(mutated)
            result = subprocess.run([str(VERIFY), "--json", "--expect", str(self.save(document)), str(image)],
                                    capture_output=True, text=True, timeout=10)
            self.assertIn(result.returncode, (0, 1), result)
            self.assertEqual(json.loads(result.stdout)["status"], result.returncode)
            self.assertEqual(image.read_bytes(), mutated)

    def test_fifo_images_are_rejected_without_blocking(self):
        document = spec(pairs(1))
        document["initial_shape"] = {"height": 1, "node_keys": {"root": 1}}
        manifest = self.save(document)
        fifo = self.work / "image.bptr"
        os.mkfifo(fifo)
        original = fifo.stat()
        alias = self.work / "image-link.bptr"
        alias.symlink_to(fifo.name)
        for path in (fifo, alias):
            for name, command in (
                ("verify", [VERIFY, "--json", "--expect", manifest, path]),
                ("generate-reuse", [GEN, "--json", "--spec", manifest, "--out", path]),
            ):
                with self.subTest(path=path.name, operation=name):
                    # No FIFO peer is present. A blocking open must fail this
                    # regression promptly instead of hanging the test runner.
                    report = self.run_tool(command, status=1, timeout=2)
                    self.assertEqual(report["invariant"], "extent")
                    self.assertFalse(report["reused"])
                    self.assertTrue(stat.S_ISFIFO(fifo.stat().st_mode))
                    self.assertEqual(fifo.stat().st_ino, original.st_ino)
                    self.assertTrue(alias.is_symlink())


if __name__ == "__main__":
    unittest.main()
