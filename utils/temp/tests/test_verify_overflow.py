"""Check verifier arithmetic with boundary templates and independent bad images."""

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


BIN = Path(__file__).resolve().parents[3] / "bin"
VERIFIER = Path(os.environ.get("TEMP_VERIFY", BIN / "temp_verify")).resolve()
GENERATOR = Path(os.environ.get("TEMP_GEN", BIN / "temp_gen")).resolve()
MIN_I64 = -(1 << 63)
MAX_I64 = (1 << 63) - 1
MIN_KEY = MIN_I64 // 2
MAX_KEY = MAX_I64 // 2


def wrapped(value):
    # Encode the bytes an overflowing implementation might leave behind,
    # without performing the same unchecked arithmetic as the verifier.
    return (value - MIN_I64) % (1 << 64) + MIN_I64


class VerifierOverflow(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="temp-verify-overflow-")
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)

    def generated(self, normal, size, height, start, interval):
        directory = self.directory / f"{normal}-{size}-{height}"
        args = [str(GENERATOR), "--dir", str(directory),
                "--node-size", str(size)]
        if normal:
            args.append("--norm")
        result = subprocess.run([*args, str(height), str(start), str(interval)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result)
        return directory / f"{height}-{start}-{interval}.bptr"

    def image(self, normal, size, leaves, inserted=None):
        # Values follow actual key_count on disk, even for partial split leaves.
        pointer = "Q" if normal else "I"
        height = 2 if len(leaves) > 1 else 1

        def node(level, keys, values, parent=0, previous=0, following=0):
            block = bytearray(size)
            struct.pack_into("=HHII", block, 0, 1 if level else 3,
                             level, len(keys), 0)
            struct.pack_into("=" + pointer * 3, block, 12,
                             parent, following, previous)
            struct.pack_into("=" + "q" * len(keys), block, 64, *keys)
            value_type = pointer if level else "q"
            struct.pack_into("=" + value_type * len(values), block,
                             64 + len(keys) * 8, *values)
            return block

        header = bytearray(size)
        header[:4] = b"BPTR"
        count = sum(map(len, leaves))
        struct.pack_into("=IIHHQI", header, 4, 0 if normal else 0x80,
                         size, 8, 8, count, height)
        struct.pack_into("=" + pointer * 4, header, 28,
                         1, 0, 0, len(leaves) + (height == 2))
        blocks = [header]
        if height == 2:
            blocks.append(node(1, [keys[0] for keys in leaves[1:]],
                               list(range(2, len(leaves) + 2))))
        for index, keys in enumerate(leaves):
            values = [inserted[1] if inserted and key == inserted[0]
                      else wrapped(key * 2) for key in keys]
            blocks.append(node(0, keys, values, parent=1 if height == 2 else 0,
                               previous=index + 1 if index else 0,
                               following=index + 3 if index + 1 < len(leaves) else 0))
        path = self.directory / "image.bptr"
        path.write_bytes(b"".join(blocks))
        return path

    def verify(self, path, start, interval, height=1, inserted=None):
        args = [str(VERIFIER), "--lay-cnt", str(height),
                "--st", str(start), "--interval", str(interval)]
        if inserted:
            args.extend(["--key", str(inserted[0]), "--val", str(inserted[1])])
        return subprocess.run([*args, str(path)], capture_output=True,
                              text=True, timeout=5)

    def assert_verified(self, *args, **kwargs):
        result = self.verify(*args, **kwargs)
        self.assertEqual(result.returncode, 0, result)
        self.assertIn(": ok", result.stdout)
        self.assertFalse(result.stderr, result.stderr)

    def assert_rejected(self, path, start, interval, message, height=1):
        result = self.verify(path, start, interval, height)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn(message, result.stdout)
        self.assertNotIn(": ok", result.stdout)
        self.assertFalse(result.stderr, result.stderr)

    def test_exact_value_boundaries_across_leaves(self):
        for normal, size in ((False, 92), (True, 104)):
            records = (size - 64) // 16 * 3 ** 2
            for start in (MIN_KEY, MAX_KEY - records + 1):
                with self.subTest(normal=normal, start=start):
                    path = self.generated(normal, size, 3, start, 1)
                    self.assert_verified(path, start, 1, height=3)

    def test_unused_final_step_may_overflow(self):
        for normal, size, start in ((False, 92, MAX_KEY),
                                    (False, 96, MIN_KEY), (True, 104, MIN_KEY)):
            with self.subTest(normal=normal, size=size):
                path = self.generated(normal, size, 1, start, MAX_I64)
                self.assert_verified(path, start, MAX_I64)

    def test_negative_intervals_do_not_require_an_unused_final_step(self):
        cases = [(False, 92, MIN_KEY, MIN_I64, [MIN_KEY]),
                 (False, 96, MAX_KEY, -MAX_I64, [MAX_KEY, MIN_KEY]),
                 (True, 104, MAX_KEY, -MAX_I64, [MAX_KEY, MIN_KEY])]
        for normal, size, start, interval, keys in cases:
            with self.subTest(normal=normal, size=size):
                self.assert_verified(self.image(normal, size, [keys]), start, interval)

    def test_valid_count_times_interval_exceeds_int64(self):
        for normal, size in ((False, 92), (True, 104)):
            records = (size - 64) // 16 * 3
            interval = MAX_I64 // (records - 1)
            with self.subTest(normal=normal):
                path = self.generated(normal, size, 2, MIN_KEY, interval)
                self.assert_verified(path, MIN_KEY, interval, height=2)

    def test_first_value_overflow_is_rejected(self):
        for normal, size in ((False, 96), (True, 104)):
            for start in (MIN_I64, MIN_KEY - 1, MAX_KEY + 1, MAX_I64):
                with self.subTest(normal=normal, start=start):
                    keys = [wrapped(start + i) for i in range(2)]
                    path = self.image(normal, size, [keys])
                    self.assert_rejected(path, start, 1, "lattice value overflows")

    def test_later_leaf_value_overflow_is_rejected(self):
        for normal, size in ((False, 92), (True, 104)):
            capacity = (size - 64) // 16
            start = MAX_KEY - capacity * 3 + 2
            leaves = [[start + leaf * capacity + i for i in range(capacity)]
                      for leaf in range(3)]
            with self.subTest(normal=normal):
                path = self.image(normal, size, leaves)
                self.assert_rejected(path, start, 1, "lattice value overflows", height=2)

    def test_key_progression_overflow_in_first_leaf(self):
        for normal, size in ((False, 96), (True, 104)):
            for start, interval in ((MAX_KEY, MAX_I64), (MIN_KEY, MIN_I64)):
                with self.subTest(normal=normal, start=start):
                    keys = [start, wrapped(start + interval)]
                    path = self.image(normal, size, [keys])
                    self.assert_rejected(path, start, interval, "lattice key overflows")

    def test_key_progression_overflow_in_later_leaf(self):
        for normal, size in ((False, 92), (True, 104)):
            capacity = (size - 64) // 16
            for start, interval in ((MIN_KEY, MAX_I64), (MAX_KEY, -MAX_I64)):
                leaves = [[wrapped(start + (leaf * capacity + i) * interval)
                           for i in range(capacity)] for leaf in range(3)]
                with self.subTest(normal=normal, start=start):
                    path = self.image(normal, size, leaves)
                    self.assert_rejected(path, start, interval,
                                         "lattice key overflows", height=2)

    def test_split_images_preserve_boundary_keys_and_arbitrary_inserted_values(self):
        for normal, size in ((False, 96), (True, 104)):
            for inserted in ((MIN_I64, MAX_I64), (0, MAX_I64), (MAX_I64, MIN_I64)):
                keys = sorted([MIN_KEY, MAX_KEY, inserted[0]])
                with self.subTest(normal=normal, inserted=inserted):
                    path = self.image(normal, size, [keys[:1], keys[1:]], inserted)
                    self.assert_verified(path, MIN_KEY, MAX_I64, inserted=inserted)

    def test_leaf_total_still_matches_geometry_without_signed_span_arithmetic(self):
        for normal, size in ((False, 96), (True, 104)):
            with self.subTest(normal=normal):
                # All three records are present, but one split must produce
                # two leaves. Count-only checks must retain this invariant.
                inserted = (0, 123)
                path = self.image(normal, size, [[MIN_KEY], [0], [MAX_KEY]], inserted)
                result = self.verify(path, MIN_KEY, MAX_I64, inserted=inserted)
                self.assertEqual(result.returncode, 1, result)
                self.assertIn("leaf count incorrect", result.stdout)
                self.assertNotIn(": ok", result.stdout)
                self.assertFalse(result.stderr, result.stderr)


if __name__ == "__main__":
    unittest.main()
