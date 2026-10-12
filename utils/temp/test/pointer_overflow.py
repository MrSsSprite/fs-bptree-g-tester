"""Exercise preflight through the real CLI without generating huge trees."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


GENERATOR, VERIFIER = map(pathlib.Path, sys.argv[1:3])
sys.argv[1:] = []


class PointerOverflowTests(unittest.TestCase):
    def generate(self, directory, *arguments):
        return subprocess.run(
            [str(GENERATOR), "--dir", str(directory), *arguments],
            capture_output=True,
            text=True,
            timeout=5,
        )

    def test_rejected_before_directory_creation_or_existing_file_reuse(self):
        cases = [
            (["8", "0", "0"], "32-bit node-index"),
            (["--norm", "13", "0", "0"], "file-offset maximum"),
            (
                ["--norm", "--node-size", "4294967295", "254", "0", "0"],
                "64-bit node-index",
            ),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            for number, (arguments, reason) in enumerate(cases):
                with self.subTest(reason=reason):
                    directory = root / str(number) / "nested"
                    result = self.generate(directory, *arguments)
                    self.assertEqual(result.returncode, 1, result)
                    self.assertIn(reason, result.stderr)
                    self.assertNotIn("reused", result.stdout)
                    self.assertFalse(directory.parent.exists())

                    # Overflow must win over reuse/fixture checking. Even an
                    # existing image must be left untouched by the refusal.
                    directory.mkdir(parents=True)
                    image = directory / f"{arguments[-3]}-0-0.bptr"
                    image.write_bytes(b"existing image remains untouched")
                    result = self.generate(directory, *arguments)
                    self.assertEqual(result.returncode, 1, result)
                    self.assertIn(reason, result.stderr)
                    self.assertNotIn("not a complete template", result.stderr)
                    self.assertEqual(
                        image.read_bytes(), b"existing image remains untouched"
                    )

    def test_small_templates_generate_verify_and_reuse(self):
        with tempfile.TemporaryDirectory() as temporary:
            for name, options in [("lite", []), ("normal", ["--norm"])]:
                with self.subTest(layout=name):
                    directory = pathlib.Path(temporary) / name
                    result = self.generate(directory, *options, "2", "0", "1")
                    self.assertEqual(result.returncode, 0, result)
                    self.assertIn("written", result.stdout)
                    image = directory / "2-0-1.bptr"
                    verified = subprocess.run(
                        [str(VERIFIER), str(image)],
                        capture_output=True,
                        text=True,
                        timeout=5,
                    )
                    self.assertEqual(verified.returncode, 0, verified)
                    result = self.generate(directory, *options, "2", "0", "1")
                    self.assertEqual(result.returncode, 0, result)
                    self.assertIn("reused", result.stdout)

    def test_negative_interval_retains_its_diagnostic(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary) / "absent" / "nested"
            result = self.generate(directory, "3", "1004", "-2")
            self.assertEqual(result.returncode, 1, result)
            self.assertIn("retry with interval 2 and st -79858", result.stderr)
            self.assertFalse(directory.parent.exists())


if __name__ == "__main__":
    unittest.main()
