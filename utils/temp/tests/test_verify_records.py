"""Check record accounting using independent, deliberately corrupted images."""

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
ORIGINAL_KEYS = [-11, -6, -1, 4]


def node(normal, level, keys, values, parent=0, previous=0, following=0):
    block = bytearray(NODE_SIZE)
    pointer = "Q" if normal else "I"
    struct.pack_into("=HHII", block, 0, 1 if level else 3, level, len(keys), 0)
    struct.pack_into("=" + pointer * 3, block, 12, parent, following, previous)
    struct.pack_into("=" + "q" * len(keys), block, 64, *keys)
    value_type = pointer if level else "q"
    struct.pack_into("=" + value_type * len(values), block,
                     64 + len(keys) * 8, *values)
    return block


class VerifierRecords(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="temp-verify-records-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def image(self, normal, leaves, header_count, height=2):
        # A height-1 original template has four records. Its split result has
        # two leaves and a new root, independently of how those records move.
        header = bytearray(NODE_SIZE)
        header[:4] = b"BPTR"
        struct.pack_into("=IIHHQI", header, 4, 0 if normal else 0x80,
                         NODE_SIZE, 8, 8, header_count, height)
        pointer = "Q" if normal else "I"
        struct.pack_into("=" + pointer * 4, header, 28,
                         1, 0, 0, 3 if len(leaves) == 2 else 1)
        blocks = [header]
        if len(leaves) == 2:
            blocks.append(node(normal, 1, [leaves[1][0]], [2, 3]))
        for index, keys in enumerate(leaves):
            blocks.append(node(
                normal, 0, keys, [key * 2 for key in keys],
                parent=1 if len(leaves) == 2 else 0,
                previous=2 if index else 0,
                following=3 if len(leaves) == 2 and index == 0 else 0,
            ))
        path = self.root / "image.bptr"
        path.write_bytes(b"".join(blocks))
        return path

    def verify(self, path, key=None, levels=1):
        args = [str(VERIFIER), "--lay-cnt", str(levels),
                "--st", str(START), "--interval", str(INTERVAL)]
        if key is not None:
            args.extend(["--key", str(key), "--val", str(key * 2)])
        return subprocess.run([*args, str(path)], capture_output=True,
                              text=True, timeout=5)

    def assert_rejected(self, path, message, key=None, levels=1):
        result = self.verify(path, key, levels)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn(message, result.stdout)
        self.assertNotIn(": ok", result.stdout)

    def test_valid_templates_at_multiple_heights(self):
        for normal in (False, True):
            for levels in (1, 2, 3):
                with self.subTest(normal=normal, levels=levels):
                    directory = self.root / f"{normal}-{levels}"
                    args = [str(GENERATOR), "--dir", str(directory),
                            "--node-size", str(NODE_SIZE)]
                    if normal:
                        args.append("--norm")
                    generated = subprocess.run(
                        [*args, str(levels), str(START), str(INTERVAL)],
                        capture_output=True, text=True, timeout=5,
                    )
                    self.assertEqual(generated.returncode, 0, generated)
                    result = self.verify(
                        directory / f"{levels}-{START}-{INTERVAL}.bptr",
                        levels=levels,
                    )
                    self.assertEqual(result.returncode, 0, result)

    def test_one_inserted_record_at_each_location(self):
        cases = [(-13, [[-13, -11], [-6, -1, 4]]),
                 (-3, [[-11, -6, -3], [-1, 4]]),
                 (7, [[-11, -6], [-1, 4, 7]])]
        for normal in (False, True):
            for key, leaves in cases:
                with self.subTest(normal=normal, key=key):
                    result = self.verify(self.image(normal, leaves, 5), key)
                    self.assertEqual(result.returncode, 0, result)

    def test_duplicate_in_first_or_later_leaf(self):
        cases = [(-13, [[-13, -13, -11], [-6, -1, 4]]),
                 (7, [[-11, -6], [-1, 4, 7, 7]])]
        for normal in (False, True):
            for key, leaves in cases:
                with self.subTest(normal=normal, key=key):
                    self.assert_rejected(self.image(normal, leaves, 5),
                                         "duplicate inserted record", key)

    def test_duplicate_across_leaf_boundary(self):
        for normal in (False, True):
            with self.subTest(normal=normal):
                path = self.image(normal, [[-11, -6, -3], [-3, -1, 4]], 5)
                self.assert_rejected(path, "duplicate inserted record", -3)

    def test_duplicate_even_when_total_record_count_matches(self):
        for normal in (False, True):
            with self.subTest(normal=normal):
                # The extra inserted record replaces an original one, keeping
                # both the header and physical total at the expected five.
                path = self.image(normal, [[-13, -13, -11], [-6, -1]], 5)
                self.assert_rejected(path, "duplicate inserted record", -13)

    def test_missing_inserted_record_at_right_endpoint(self):
        for normal in (False, True):
            with self.subTest(normal=normal):
                path = self.image(normal, [[-11, -6], [-1, 4]], 5)
                self.assert_rejected(path, "inserted record missing", 7)

    def test_physical_total_with_exactly_one_inserted_record(self):
        for normal in (False, True):
            for right in ([-6, -1], [-6, -1, 4, 9]):
                actual_count = 2 + len(right)
                for header_count in (5, actual_count):
                    with self.subTest(normal=normal, actual=actual_count,
                                      header=header_count):
                        # The inserted record occurs exactly once. The header
                        # may even agree with the corrupted physical total;
                        # expected records must come from the original shape.
                        path = self.image(normal, [[-13, -11], right],
                                          header_count)
                        self.assert_rejected(path, "leaf record count incorrect",
                                             -13)

    def test_physical_total_without_inserted_record(self):
        for normal in (False, True):
            for header_count in (3, 4):
                with self.subTest(normal=normal, header=header_count):
                    path = self.image(normal, [ORIGINAL_KEYS[:-1]], header_count,
                                      height=1)
                    self.assert_rejected(path, "leaf record count incorrect")

    def test_header_count_must_match_computed_total(self):
        for normal in (False, True):
            for header_count in (4, 6):
                with self.subTest(normal=normal, header=header_count):
                    path = self.image(normal, [[-13, -11], [-6, -1, 4]],
                                      header_count)
                    self.assert_rejected(path, "record count incorrect", -13)

    def test_expected_record_count_overflow_is_rejected(self):
        for normal in (False, True):
            with self.subTest(normal=normal):
                # A forged height must fail checked geometry arithmetic before
                # traversal, rather than wrapping to a plausible record count.
                path = self.image(normal, [ORIGINAL_KEYS], 4, height=65)
                self.assert_rejected(path, "expected record count overflows",
                                     levels=65)


if __name__ == "__main__":
    unittest.main()
