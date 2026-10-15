"""Check occupancy with independent split images whose record totals stay valid."""

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


VERIFIER = Path(os.environ.get(
    "TEMP_VERIFY", Path(__file__).resolve().parents[3] / "bin/temp_verify"
)).resolve()
LAYOUTS = [(False, size) for size in (92, 96, 112, 128, 144, 512)] + [
    (True, size) for size in (104, 112, 128, 144, 512)
]


class Image:
    def __init__(self, normal, size):
        self.size = size
        self.pointer = "Q" if normal else "I"
        self.version = 0 if normal else 0x80
        self.leaf_max = (size - 64) // 16
        pointer_size = 8 if normal else 4
        self.children_max = (size - 64 - pointer_size) // (8 + pointer_size) + 1

    def node(self, level, keys, values, parent=0, previous=0, following=0):
        block = bytearray(self.size)
        struct.pack_into("=HHII", block, 0, 1 if level else 3, level, len(keys), 0)
        struct.pack_into("=" + self.pointer * 3, block, 12,
                         parent, following, previous)
        struct.pack_into("=" + "q" * len(keys), block, 64, *keys)
        value_type = self.pointer if level else "q"
        struct.pack_into("=" + value_type * len(values), block,
                         64 + 8 * len(keys), *values)
        return block

    def header(self, height, records, nodes):
        block = bytearray(self.size)
        block[:4] = b"BPTR"
        struct.pack_into("=IIHHQI", block, 4, self.version, self.size,
                         8, 8, records, height)
        struct.pack_into("=" + self.pointer * 4, block, 28, 1, 0, 0, nodes)
        return block

    def template(self):
        keys = list(range(0, 2 * self.leaf_max, 2))
        return self.header(1, len(keys), 1) + self.node(
            0, keys, [2 * key for key in keys]
        )

    def split(self, levels=1, leaf_counts=None, branch_children=None):
        # Splitting a full height-1/2 image adds one leaf, optionally one branch,
        # and a new root. Build all keys, links and separators without core code.
        original_leaves = 1 if levels == 1 else self.children_max
        records = self.leaf_max * original_leaves
        keys = [-1, *range(0, 2 * records, 2)]
        if leaf_counts is None:
            half = (self.leaf_max + 1) // 2
            leaf_counts = [half, self.leaf_max + 1 - half]
            leaf_counts += [self.leaf_max] * (original_leaves - 1)
        assert len(leaf_counts) == original_leaves + 1
        assert sum(leaf_counts) == len(keys)
        leaves, offset = [], 0
        for count in leaf_counts:
            leaves.append(keys[offset:offset + count])
            offset += count
        first_leaf = 2 if levels == 1 else 4
        leaf_indices = list(range(first_leaf, first_leaf + len(leaves)))
        blocks = [self.header(levels + 1, len(keys), first_leaf + len(leaves) - 1)]
        if levels == 1:
            blocks.append(self.node(1, [leaves[1][0]], leaf_indices))
            parents = [1] * len(leaves)
        else:
            if branch_children is None:
                half = len(leaves) // 2
                branch_children = [half, len(leaves) - half]
            assert len(branch_children) == 2
            assert sum(branch_children) == len(leaves)
            boundary = branch_children[0]
            blocks.append(self.node(2, [leaves[boundary][0]], [2, 3]))
            parents, offset = [], 0
            for index, count in enumerate(branch_children):
                separators = [leaf[0] for leaf in leaves[offset + 1:offset + count]]
                blocks.append(self.node(
                    1, separators, leaf_indices[offset:offset + count], parent=1,
                    previous=2 if index else 0, following=0 if index else 3,
                ))
                parents += [index + 2] * count
                offset += count
        for index, leaf in enumerate(leaves):
            blocks.append(self.node(
                0, leaf, [2 * key for key in leaf], parent=parents[index],
                previous=leaf_indices[index - 1] if index else 0,
                following=leaf_indices[index + 1] if index + 1 < len(leaves) else 0,
            ))
        return bytearray(b"".join(blocks))

    def set_count(self, data, index, count):
        struct.pack_into("=I", data, self.size * index + 4, count)


class VerifierOccupancy(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="temp-verify-occupancy-")
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "image.bptr"

    def verify(self, data, levels=1, inserted=True):
        self.path.write_bytes(data)
        args = [str(VERIFIER), "--lay-cnt", str(levels), "--st", "0", "--interval", "2"]
        if inserted:
            args += ["--key", "-1", "--val", "-2"]
        return subprocess.run([*args, str(self.path)], capture_output=True,
                              text=True, timeout=5)

    def assert_valid(self, data, **kwargs):
        result = self.verify(data, **kwargs)
        self.assertEqual(result.returncode, 0, result)
        self.assertIn(": ok", result.stdout)

    def assert_rejected(self, data, message, **kwargs):
        result = self.verify(data, **kwargs)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn(message, result.stdout)
        self.assertNotIn(": ok", result.stdout)

    def test_leaf_split_at_legal_bounds(self):
        for normal, size in LAYOUTS:
            image = Image(normal, size)
            half = (image.leaf_max + 1) // 2
            for counts in ([half, image.leaf_max + 1 - half],
                           [image.leaf_max + 1 - half, half]):
                with self.subTest(normal=normal, size=size, counts=counts):
                    self.assert_valid(image.split(leaf_counts=counts))

    def test_underfilled_leaf_with_correct_records_and_links(self):
        for normal, size in LAYOUTS:
            image = Image(normal, size)
            if image.leaf_max < 3:
                continue
            below_minimum = (image.leaf_max + 1) // 2 - 1
            counts = [below_minimum, image.leaf_max + 1 - below_minimum]
            for counts in (counts, counts[::-1]):
                with self.subTest(normal=normal, size=size, counts=counts):
                    self.assert_rejected(image.split(leaf_counts=counts),
                                         "leaf node underfilled")

    def test_branch_split_and_one_key_root_at_legal_bounds(self):
        for normal, size in LAYOUTS:
            image = Image(normal, size)
            half = (image.children_max + 1) // 2
            counts = [half, image.children_max + 1 - half]
            for counts in (counts, counts[::-1]):
                with self.subTest(normal=normal, size=size, children=counts):
                    self.assert_valid(image.split(2, branch_children=counts), levels=2)

    def test_underfilled_branch_with_correct_records_and_links(self):
        for normal, size in LAYOUTS:
            image = Image(normal, size)
            below_minimum = (image.children_max + 1) // 2 - 1
            counts = [below_minimum, image.children_max + 1 - below_minimum]
            for counts in (counts, counts[::-1]):
                with self.subTest(normal=normal, size=size, children=counts):
                    self.assert_rejected(image.split(2, branch_children=counts),
                                         "internal node underfilled", levels=2)

    def test_full_leaf_root_is_valid(self):
        for normal, size in LAYOUTS:
            with self.subTest(normal=normal, size=size):
                self.assert_valid(Image(normal, size).template(), inserted=False)

    def test_empty_internal_root_is_rejected(self):
        for normal in (False, True):
            with self.subTest(normal=normal):
                image = Image(normal, 128)
                data = image.split()
                image.set_count(data, 1, 0)
                self.assert_rejected(data, "internal node underfilled")

    def test_overfull_leaf_is_rejected_before_decoding(self):
        for normal in (False, True):
            image = Image(normal, 128)
            for index in (1, 2, 3):
                with self.subTest(normal=normal, index=index):
                    data = image.template() if index == 1 else image.split()
                    image.set_count(data, index, image.leaf_max + 1)
                    self.assert_rejected(data, "leaf node overfull", inserted=index != 1)

    def test_overfull_branch_is_rejected_before_decoding(self):
        for normal in (False, True):
            image = Image(normal, 128)
            for index in (1, 2, 3):
                with self.subTest(normal=normal, index=index):
                    data = image.split(2)
                    image.set_count(data, index, image.children_max)
                    self.assert_rejected(data, "internal node overfull", levels=2)


if __name__ == "__main__":
    unittest.main()
