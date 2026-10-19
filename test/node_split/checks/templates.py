"""Check fixture loading and the exact original-shape verifier arguments."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
BINARY = Path(sys.argv[1]).resolve()


def run(args, work):
    result = subprocess.run([str(BINARY), *args], cwd=work, capture_output=True)
    assert result.returncode == 0, (args, result.stdout.decode(), result.stderr.decode())
    assert not result.stderr, result.stderr.decode()
    return result


(ROOT / ".cache").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="split-templates-", dir=ROOT / ".cache") as tmp:
    work = Path(tmp)
    result = run([], work)
    assert b"4 Tests 0 Failures 0 Ignored" in result.stdout, result.stdout.decode()
    print(result.stdout.decode(), end="")

    (work / "bin").mkdir()
    verifier = work / "bin/temp_verify"
    verifier.write_text(
        f"#!{sys.executable}\n"
        "import json, sys\n"
        "from pathlib import Path\n"
        "Path('args.json').write_text(json.dumps(sys.argv[1:]))\n"
        "print('successful tool output stays quiet')\n"
    )
    verifier.chmod(0o755)
    scalars = ["--lay-cnt", "4", "--st", "0", "--interval", "16"]
    original = ["--node-keys", "root=1", "--node-keys", "0=4",
                "--node-keys", "0.1=2"]
    inserted = ["--key", "1", "--val", "2"]
    totals = ["--expect-height", "4", "--expect-nodes", "254",
              "--expect-leaves", "211"]
    for filename, shape in (
        ("4-0-16.bptr", []),
        ("4-0-16--keys-root=1,0=4,0.1=2.bptr", original),
    ):
        for mode in ("--verify-pristine", "--verify-insert"):
            result = run([mode, filename], work)
            assert not result.stdout, result.stdout.decode()
            expected = [*scalars, *shape]
            if mode == "--verify-insert":
                expected += inserted
                if shape:
                    expected += totals
            expected += ["instance.bptr"]
            actual = json.loads((work / "args.json").read_text())
            assert actual == expected, (mode, filename, expected, actual)
    print("full/custom original shape forwarding: PASS")
