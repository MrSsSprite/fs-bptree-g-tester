"""Run real tools against isolated fixtures; leave existing inputs untouched.

Run from the repository root after building: python3 test/node_split/checks/integration.py
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
BIN = ROOT / "bin"


def run(argv, cwd=ROOT, env=None):
    result = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env,
                            capture_output=True)
    assert result.returncode == 0, (argv, result.stdout.decode(), result.stderr.decode())
    return result


def fixture(directory, height, size=112, normal=False, interval=5, start=-11):
    argv = [BIN / "temp_gen", "--dir", directory, "--node-size", size]
    if normal:
        argv.append("--norm")
    run([*argv, height, start, interval])


def assert_success(result, templates):
    output = result.stdout.decode()
    assert not result.stderr, result.stderr.decode()
    assert "\r" not in output and "skip " not in output
    assert "2 Tests 0 Failures 0 Ignored" in output
    assert output.count(":PASS") == 2
    assert output.count("Tests 0 Failures") == 1
    assert output.count(".bptr: PASS") == templates * 2


# Each tool wrapper runs in a separate process, just like the real tools.
# An empty directory before EVERY copy proves that the previous image was
# removed, rather than merely bounding the number seen by a polling monitor.
WRAPPER = r'''
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

tool = Path(sys.argv[0]).name
args = sys.argv[1:]
split_dir = Path("bptr_files/temp/split")
is_instance = tool == "temp_inst" or "--key" in args
if is_instance:
    files = list(split_dir.glob("*.bptr"))
    if tool == "temp_inst":
        assert not files, files
    else:
        assert len(files) == 1 and files[0] == Path(args[-1]), files
        with files[0].open("rb") as image:
            header = image.read(64)
        expected_height = int(args[args.index("--lay-cnt") + 1]) + 1
        assert struct.unpack_from("=I", header, 24)[0] == expected_height
result = subprocess.run([str(Path(os.environ["SPLIT_CHECK_BIN"]) / tool), *args])
status = result.returncode
if is_instance:
    if tool == "temp_verify" and os.environ.get("SPLIT_CHECK_FAIL_VERIFY"):
        if Path(args[-1]).name.endswith("-2.bptr"):
            print("injected verifier failure", file=sys.stderr)
            status = 1
    with open(os.environ["SPLIT_CHECK_TRACE"], "a") as trace:
        trace.write(json.dumps([tool, args[-1], status]) + "\n")
sys.exit(status)
'''


def instrumented(parent, failure=False):
    work = parent / ("failure" if failure else "ordering")
    tools = work / "bin"
    tools.mkdir(parents=True)
    for name in ("temp_inst", "temp_verify"):
        wrapper = tools / name
        wrapper.write_text(f"#!{sys.executable}\n" + WRAPPER)
        wrapper.chmod(0o755)
    templates = work / "templates"
    fixture(templates, 4)
    trace = work / "events.jsonl"
    env = dict(os.environ, SPLIT_CHECK_BIN=str(BIN), SPLIT_CHECK_TRACE=str(trace))
    if failure:
        env["SPLIT_CHECK_FAIL_VERIFY"] = "1"
    else:
        env.pop("SPLIT_CHECK_FAIL_VERIFY", None)
    result = subprocess.run([str(BIN / "node_split"), str(templates)], cwd=work,
                            env=env, capture_output=True)
    events = [json.loads(line) for line in trace.read_text().splitlines()]
    assert len(events) % 2 == 0
    for copy, verify in zip(events[::2], events[1::2]):
        assert copy[0] == "temp_inst" and copy[2] == 0
        assert verify[0] == "temp_verify" and copy[1] == verify[1]
    files = list((work / "bptr_files/temp/split").glob("*.bptr"))
    if failure:
        assert result.returncode == 1 and len(events) == 6
        assert len(files) == 1 and files[0].name == "4-2.bptr"
        assert events[-1][2] == 1
        assert b"injected verifier failure" in result.stderr
        assert b"failed to verify the instance" in result.stdout
        assert b"verify bptr_files/temp/split/4-2.bptr:" in result.stdout
        assert b"4-*.bptr: PASS" not in result.stdout
    else:
        assert result.returncode == 0
        assert_success(result, 1)
        assert len(events) == 2 * 67 and not files
    print(f"real tools: {'failure retention' if failure else 'one-instance ordering'}: PASS",
          flush=True)


def main():
    (ROOT / ".cache").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="split-integration-", dir=ROOT / ".cache") as temp:
        work = Path(temp)
        for name, normal, size, heights in (
            ("small", False, 512, (1, 2, 3)),
            ("lite", False, 112, (4, 5)),
            ("normal", True, 112, (4, 5)),
            ("single-key", False, 92, (4,)),
            ("higher-dimension", False, 112, (8,)),
            ("default-four", False, 512, (4,)),
        ):
            run_root = work / name
            tools = run_root / "bin"
            tools.mkdir(parents=True)
            for tool in ("temp_inst", "temp_verify"):
                (tools / tool).symlink_to(BIN / tool)
            directory = run_root / "templates"
            for height in heights:
                fixture(directory, height, size, normal)
            result = run([BIN / "node_split", directory], cwd=run_root)
            assert_success(result, len(heights))
            assert not list((run_root / "bptr_files/temp/split").glob("*.bptr"))
            print(f"{name}, heights {heights}: PASS", flush=True)
        for name, height, size, start, interval in (
            ("dense-skip", 4, 112, -11, 1),
            ("value-overflow", 1, 92, (2**63 - 1) // 2, 4),
        ):
            run_root = work / name
            tools = run_root / "bin"
            tools.mkdir(parents=True)
            for tool in ("temp_inst", "temp_verify"):
                (tools / tool).symlink_to(BIN / tool)
            directory = run_root / "templates"
            fixture(directory, height, size, interval=interval, start=start)
            result = subprocess.run([str(BIN / "node_split"), str(directory)],
                                    cwd=run_root, capture_output=True)
            assert not result.stderr, result.stderr.decode()
            assert not (run_root / "bptr_files/temp/split").exists()
            if name == "dense-skip":
                assert result.returncode == 0
                assert b"leaves no room between two keys" in result.stdout
            else:
                assert result.returncode == 1
                assert b"inserted key or value is not representable" in result.stdout
                assert b"test_temp:PASS" in result.stdout
            print(f"{name}, no instances created: PASS", flush=True)
        instrumented(work)
        instrumented(work, failure=True)


if __name__ == "__main__":
    main()
