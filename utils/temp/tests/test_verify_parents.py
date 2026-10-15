"""Check parent links using generated trees and independent split images."""

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


BIN = Path(__file__).resolve().parents[3] / "bin"
VERIFIER = Path(os.environ.get("TEMP_VERIFY", BIN / "temp_verify")).resolve()
GENERATOR = Path(os.environ.get("TEMP_GEN", BIN / "temp_gen")).resolve()
NODE_SIZE = 128
START = -11
INTERVAL = 5


class Image:
    def __init__(self, data):
        self.data = bytearray(data)
        version = struct.unpack_from("=I", data, 4)[0]
        self.pointer = "I" if version & 0x80 else "Q"
        self.pointer_size = struct.calcsize(self.pointer)
        self.root = self.read_pointer(28)

    def read_pointer(self, offset):
        return struct.unpack_from("=" + self.pointer, self.data, offset)[0]

    def level(self, index):
        return struct.unpack_from("=H", self.data, index * NODE_SIZE + 2)[0]

    def key_count(self, index):
        return struct.unpack_from("=I", self.data, index * NODE_SIZE + 4)[0]

    def branches(self, level=None):
        return [index for index in range(1, len(self.data) // NODE_SIZE)
                if self.level(index) > 0 and
                (level is None or self.level(index) == level)]

    def children(self, index):
        offset = index * NODE_SIZE + 64 + self.key_count(index) * 8
        return [self.read_pointer(offset + child * self.pointer_size)
                for child in range(self.key_count(index) + 1)]

    def set_parent(self, index, parent):
        struct.pack_into("=" + self.pointer, self.data,
                         index * NODE_SIZE + 12, parent)

    def replace_child(self, parent, position, child):
        offset = parent * NODE_SIZE + 64 + self.key_count(parent) * 8
        struct.pack_into("=" + self.pointer, self.data,
                         offset + position * self.pointer_size, child)
        # Keep the separator correct so rejection must come from the parent
        # relationship, rather than the existing separator-key assertions.
        if position:
            leaf = child
            while self.level(leaf):
                leaf = self.children(leaf)[0]
            key = struct.unpack_from("=q", self.data, leaf * NODE_SIZE + 64)[0]
            struct.pack_into("=q", self.data,
                             parent * NODE_SIZE + 64 + (position - 1) * 8, key)

    def append_child(self, parent, child):
        count = self.key_count(parent)
        children = self.children(parent)
        keys = struct.unpack_from("=" + "q" * count, self.data,
                                  parent * NODE_SIZE + 64)
        struct.pack_into("=I", self.data, parent * NODE_SIZE + 4, count + 1)
        # Moving the value array preserves existing keys and child references.
        struct.pack_into("=" + "q" * (count + 1), self.data,
                         parent * NODE_SIZE + 64, *keys, 0)
        struct.pack_into("=" + self.pointer * (count + 2), self.data,
                         parent * NODE_SIZE + 64 + (count + 1) * 8,
                         *children, child)
        self.replace_child(parent, count + 1, child)


def split_image(normal):
    # Encode a height-2 full template after one leaf split and one root split.
    # This fixture does not call the split implementation being checked.
    children = 4 if normal else 6
    pointer = "Q" if normal else "I"
    gap = (children // 2) * 4 + 2
    inserted = START + gap * INTERVAL - INTERVAL // 2
    leaves = [[START + (leaf * 4 + i) * INTERVAL for i in range(4)]
              for leaf in range(children)]
    expanded = sorted([*leaves[children // 2], inserted])
    leaves[children // 2:children // 2 + 1] = [expanded[:3], expanded[3:]]
    left_children = children // 2 + 1

    def node(level, keys, values, parent=0, previous=0, following=0):
        block = bytearray(NODE_SIZE)
        struct.pack_into("=HHII", block, 0, 1 if level else 3, level, len(keys), 0)
        struct.pack_into("=" + pointer * 3, block, 12, parent, following, previous)
        struct.pack_into("=" + "q" * len(keys), block, 64, *keys)
        value_type = pointer if level else "q"
        struct.pack_into("=" + value_type * len(values), block,
                         64 + len(keys) * 8, *values)
        return block

    header = bytearray(NODE_SIZE)
    header[:4] = b"BPTR"
    struct.pack_into("=IIHHQI", header, 4, 0 if normal else 0x80,
                     NODE_SIZE, 8, 8, children * 4 + 1, 3)
    struct.pack_into("=" + pointer * 4, header, 28, 1, 0, 0, children + 4)
    blocks = [header, node(2, [leaves[left_children][0]], [2, 3])]
    for index, begin, end in ((2, 0, left_children),
                              (3, left_children, len(leaves))):
        blocks.append(node(1, [leaves[i][0] for i in range(begin + 1, end)],
                           list(range(4 + begin, 4 + end)), parent=1,
                           previous=2 if index == 3 else 0,
                           following=3 if index == 2 else 0))
    for i, keys in enumerate(leaves):
        blocks.append(node(0, keys, [key * 2 for key in keys],
                           parent=2 if i < left_children else 3,
                           previous=3 + i if i else 0,
                           following=5 + i if i + 1 < len(leaves) else 0))
    return Image(b"".join(blocks)), inserted


class VerifierParents(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="temp-verify-parents-")
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)

    def generated(self, normal, height=4):
        directory = self.directory / f"{normal}-{height}"
        args = [str(GENERATOR), "--dir", str(directory), "--node-size", str(NODE_SIZE)]
        if normal:
            args.append("--norm")
        result = subprocess.run([*args, str(height), str(START), str(INTERVAL)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result)
        return Image((directory / f"{height}-{START}-{INTERVAL}.bptr").read_bytes())

    def verify(self, image, height=4, key=None):
        path = self.directory / "image.bptr"
        path.write_bytes(image.data)
        args = [str(VERIFIER), "--lay-cnt", str(height),
                "--st", str(START), "--interval", str(INTERVAL)]
        if key is not None:
            args.extend(["--key", str(key), "--val", str(key * 2)])
        return subprocess.run([*args, str(path)], capture_output=True,
                              text=True, timeout=5)

    def assert_rejected(self, image, message="parent", height=4, key=None):
        result = self.verify(image, height, key)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn(message, result.stdout)
        self.assertNotIn(": ok", result.stdout)

    def test_valid_generated_templates(self):
        for normal in (False, True):
            for height in (1, 2, 3, 4):
                with self.subTest(normal=normal, height=height):
                    result = self.verify(self.generated(normal, height), height)
                    self.assertEqual(result.returncode, 0, result)

    def test_stale_parent_on_middle_branch(self):
        for normal in (False, True):
            original = self.generated(normal)
            for level in (1, 2):
                branches = original.branches(level)
                target = branches[len(branches) // 2]
                with self.subTest(normal=normal, level=level, target=target):
                    image = Image(original.data)
                    image.set_parent(target, branches[0])
                    self.assert_rejected(image)

    def test_every_nonroot_branch_requires_a_parent(self):
        for normal in (False, True):
            original = self.generated(normal)
            for target in original.branches():
                if target == original.root:
                    continue
                with self.subTest(normal=normal, target=target):
                    image = Image(original.data)
                    image.set_parent(target, 0)
                    self.assert_rejected(image)

    def test_wrong_parent_at_correct_level(self):
        for normal in (False, True):
            original = self.generated(normal)
            parents = original.branches(2)
            for parent in parents:
                other = parents[(parents.index(parent) + 1) % len(parents)]
                for target in original.children(parent):
                    with self.subTest(normal=normal, parent=parent, target=target):
                        image = Image(original.data)
                        image.set_parent(target, other)
                        self.assert_rejected(image)

    def test_unreadable_or_leaf_parent(self):
        for normal in (False, True):
            original = self.generated(normal)
            branches = original.branches(1)
            target = branches[len(branches) // 2]
            # Metadata preflight rejects an unreadable parent before the
            # ownership check can report a parent-specific diagnostic.
            for parent, message in (
                (len(original.data) // NODE_SIZE + 1,
                 "failed to read node for occupancy check"),
                (original.children(target)[0], "parent"),
                (target, "parent"),
            ):
                with self.subTest(normal=normal, parent=parent):
                    image = Image(original.data)
                    image.set_parent(target, parent)
                    self.assert_rejected(image, message)

    def test_parent_must_reference_branch_exactly_once(self):
        for normal in (False, True):
            original = self.generated(normal)
            parents = original.branches(2)
            parent = parents[len(parents) // 2]
            image = Image(original.data)
            image.replace_child(parent, 1, image.children(parent)[2])
            with self.subTest(normal=normal):
                self.assert_rejected(image, "parent must reference it exactly once")

    def test_branch_missing_from_its_parent(self):
        for normal in (False, True):
            image = self.generated(normal)
            parents = image.branches(2)
            parent = parents[len(parents) // 2]
            # Borrow a different parent's child, without removing its original
            # reference. The replaced branch is now an orphan in its chain.
            image.replace_child(parent, 1, image.children(parents[0])[1])
            with self.subTest(normal=normal):
                self.assert_rejected(image, "parent must reference it exactly once")

    def test_root_requires_zero_parent(self):
        for normal in (False, True):
            for height in (1, 4):
                with self.subTest(normal=normal, height=height):
                    image = self.generated(normal, height)
                    image.set_parent(image.root, image.root)
                    self.assert_rejected(image, "root parent != 0", height)

    def test_split_images_and_their_internal_parents(self):
        for normal in (False, True):
            original, key = split_image(normal)
            with self.subTest(normal=normal, valid=True):
                result = self.verify(original, height=2, key=key)
                self.assertEqual(result.returncode, 0, result)
            for target in original.branches():
                with self.subTest(normal=normal, target=target):
                    image = Image(original.data)
                    image.set_parent(target, target)
                    self.assert_rejected(image, height=2, key=key)

    def test_child_cannot_be_claimed_by_another_parent(self):
        for normal in (False, True):
            image, key = split_image(normal)
            # All original back-pointers and parent memberships remain valid.
            # An extra tail reference in the rightmost branch claims a leaf
            # owned by the left branch; the leaf-chain walk never consumes it.
            image.append_child(3, image.children(2)[0])
            with self.subTest(normal=normal):
                self.assert_rejected(image, "child parent != its owning internal node",
                                     height=2, key=key)


if __name__ == "__main__":
    unittest.main()
