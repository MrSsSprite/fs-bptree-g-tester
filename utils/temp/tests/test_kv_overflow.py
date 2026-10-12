"""Exercise the generator's key/value preflight through its executable."""

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


GENERATOR = Path(os.environ.get(
    "TEMP_GEN", Path(__file__).resolve().parents[3] / "bin/temp_gen"
)).resolve()
MIN_KEY = -(1 << 62)
MAX_KEY = (1 << 62) - 1
MAX_I64 = (1 << 63) - 1


class KeyValueOverflow(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="temp-gen-kv-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def generate(self, name, levels, start, interval, size=512, norm=False):
        directory = self.root / name / "templates"
        args = [str(GENERATOR), "--dir", str(directory), "--node-size", str(size)]
        if norm:
            args.append("--norm")
        args.extend(map(str, (levels, start, interval)))
        result = subprocess.run(args, capture_output=True, text=True, timeout=5)
        return result, directory / f"{levels}-{start}-{interval}.bptr"

    def assert_rejected(self, *args, **kwargs):
        result, path = self.generate(*args, **kwargs)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn("key/value overflow", result.stderr)
        self.assertNotIn("reused", result.stdout)
        self.assertFalse(path.parent.exists(), "preflight created a directory")

    def assert_generated(self, name, levels, start, interval, size=512, norm=False):
        result, path = self.generate(name, levels, start, interval, size, norm)
        self.assertEqual(result.returncode, 0, result)
        image = path.read_bytes()
        expected = start
        count = 0
        capacity = (size - 64) // 16
        for offset in range(size, len(image), size):
            flags, level, key_count = struct.unpack_from("=HHI", image, offset)
            if not flags & 2:
                continue
            self.assertEqual(level, 0)
            self.assertEqual(key_count, capacity)
            for index in range(key_count):
                key = struct.unpack_from("=q", image, offset + 64 + index * 8)[0]
                value = struct.unpack_from(
                    "=q", image, offset + 64 + capacity * 8 + index * 8
                )[0]
                self.assertEqual(key, expected)
                self.assertEqual(value, expected * 2)
                expected += interval
                count += 1
        self.assertEqual(struct.unpack_from("=Q", image, 16)[0], count)
        fanout = (size - 64 - (8 if norm else 4)) // (16 if norm else 12) + 1
        self.assertEqual(count, capacity * fanout ** (levels - 1))

    def test_first_value_out_of_range(self):
        for norm in (False, True):
            for start in (MIN_KEY - 1, MAX_KEY + 1, -(1 << 63), MAX_I64):
                with self.subTest(norm=norm, start=start):
                    self.assert_rejected(str(start) + str(norm), 1, start, 0, norm=norm)

    def test_exact_value_boundaries_with_zero_interval(self):
        for norm in (False, True):
            for start in (MIN_KEY, MAX_KEY):
                with self.subTest(norm=norm, start=start):
                    self.assert_generated(str(start) + str(norm), 1, start, 0, norm=norm)

    def test_largest_key_and_value_boundary_across_multiple_leaves(self):
        for norm, size, levels in ((False, 92, 3), (True, 104, 3),
                                   (False, 512, 2), (True, 512, 2)):
            fanout = (size - 64 - (8 if norm else 4)) // (16 if norm else 12) + 1
            records = (size - 64) // 16 * fanout ** (levels - 1)
            start = MAX_KEY - (records - 1)
            with self.subTest(norm=norm, size=size):
                name = f"{norm}-{size}"
                self.assert_generated(name, levels, start, 1, size, norm)
                self.assert_rejected(name + "-overflow", levels, start + 1, 1, size, norm)

    def test_key_progression_overflow(self):
        for start in (0, MIN_KEY):
            with self.subTest(start=start):
                self.assert_rejected(str(start), 1, start, MAX_I64, size=112)

    def test_unused_final_step_may_overflow(self):
        self.assert_generated("single-key", 1, MAX_KEY, MAX_I64, size=92)
        # Two keys span the entire allowed range.  Computing the next key,
        # which will never be stored, would overflow int64_t.
        self.assert_generated("two-keys", 1, MIN_KEY, MAX_I64, size=96)

    def test_huge_layout_preflight_does_not_overflow(self):
        for norm in (False, True):
            with self.subTest(norm=norm):
                self.assert_rejected(str(norm), 254, MIN_KEY, 1, norm=norm)
                self.assert_rejected(str(norm) + "-large-node", 254, 0, 1,
                                     size=(1 << 32) - 1, norm=norm)

    def test_existing_image_is_not_reused_or_modified(self):
        directory = self.root / "existing" / "templates"
        directory.mkdir(parents=True)
        path = directory / f"1-{MAX_KEY + 1}-0.bptr"
        path.write_bytes(b"existing image must survive a rejected request")
        before = path.read_bytes()
        result, actual_path = self.generate("existing", 1, MAX_KEY + 1, 0)
        self.assertEqual(actual_path, path)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn("key/value overflow", result.stderr)
        self.assertNotIn("reused", result.stdout)
        self.assertEqual(path.read_bytes(), before)

    def test_negative_interval_still_reports_ascending_request(self):
        result, path = self.generate("negative", 3, 1004, -2)
        self.assertEqual(result.returncode, 1, result)
        self.assertIn("interval is negative", result.stderr)
        self.assertIn("retry with interval 2 and st -79858", result.stderr)
        self.assertFalse(path.parent.exists())


if __name__ == "__main__":
    unittest.main()
