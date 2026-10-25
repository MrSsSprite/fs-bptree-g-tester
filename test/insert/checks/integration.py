"""Validate tester mechanics independently of insertion's conformance verdict.

The adversaries wrap public calls in tester-only object files. No core source
is read, changed, or copied. The actual library can fail conformance while this
suite passes: these checks prove that distinct bad behaviors are detected.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
UNIT = ROOT / "test/insert"
GROUPS = ["empty", "permutations", "growth", "comparators", "binary", "equivalence",
          "ownership", "fixtures", "cache", "cache-errors", "io-errors"]


def run(arguments, *, cwd, env=None, expected=0):
    result = subprocess.run([str(value) for value in arguments], cwd=cwd,
                            env=env, capture_output=True, timeout=30)
    assert result.returncode == expected, (
        arguments, result.returncode, result.stdout.decode(errors="replace"),
        result.stderr.decode(errors="replace"))
    return result


def build_proxy(work):
    """Redirect tester calls only; link the existing, unchanged core objects."""
    includes = [ROOT / "core", ROOT / "core/src", ROOT / "test/helper",
                ROOT / "deps/unity/src", ROOT / "utils/temp/src", UNIT / "src"]
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-g",
             "-D_POSIX_C_SOURCE=200809L", "-DBPTR_TESTING",
             *[f"-I{path}" for path in includes]]
    objects = []
    for source in sorted((UNIT / "src").glob("*.c")):
        target = work / (source.stem + ".o")
        run([*compiler, *flags, "-Dbptr_insert=checked_bptr_insert",
             "-Dbptr_unload=checked_bptr_unload", "-c", source, "-o", target],
            cwd=ROOT)
        objects.append(target)
    proxy = work / "proxy.o"
    run([*compiler, *flags, "-c", UNIT / "checks/proxy.c", "-o", proxy], cwd=ROOT)
    dependencies = [*sorted((ROOT / "obj/core").glob("*.o")),
                    *sorted((ROOT / "obj/test_util").glob("*.o")),
                    ROOT / "obj/unity.o", ROOT / "obj/insert/common/temp_image.o"]
    assert dependencies and all(path.is_file() for path in dependencies), (
        "build the unit first: make insert", dependencies)
    binary = work / "insert-proxy"
    run([*compiler, *objects, proxy, *dependencies, "-o", binary], cwd=ROOT)
    return binary


def snapshots(directory):
    return {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob("*") if path.is_file()}


def prepare_work(parent, name):
    work = parent / name
    work.mkdir()
    directory = work / "cases"
    directory.mkdir()
    (directory / "sentinel.txt").write_bytes(b"unrelated bytes\0\xff\n")
    (directory / "existing").mkdir()
    (directory / "existing/notes.txt").write_text("preserve existing files\n")
    return work, directory, snapshots(directory)


def preserved(directory, before):
    for path, digest in before.items():
        actual = hashlib.sha256((directory / path).read_bytes()).hexdigest()
        assert actual == digest, (path, digest, actual)


def cli_checks(binary, parent):
    work = parent / "cli"
    work.mkdir()
    result = run([binary, "--list"], cwd=work)
    listed = result.stdout.decode().splitlines()
    assert len(listed) == len(set(listed)) and set(GROUPS) <= set(listed), result.stdout
    assert not result.stderr and not list(work.iterdir())
    invalid = [
        ["--unknown", "value"], ["--case"], ["--case", "absent"],
        ["--layout", "other"], ["--case", "cache", "--case", "cache"],
        ["--layout", "normal", "--layout", "lite"],
        ["--seed", "1", "--seed", "2"], ["--work-dir", ""],
        ["--list", "--case", "cache"],
    ]
    invalid += [["--seed", value] for value in
                ("", "-1", " -1", "18446744073709551616", "1oops", "0x")]
    for ordinal, arguments in enumerate(invalid):
        directory = work / f"must-not-exist-{ordinal}"
        extra = [] if "--work-dir" in arguments else ["--work-dir", directory]
        result = run([binary, *arguments, *extra], cwd=work, expected=1)
        assert b"Tests" not in result.stdout, result.stdout
        assert not directory.exists(), (arguments, directory)
    assert not list(work.iterdir())
    print("insert harness: list and malformed CLI rejected before work: PASS", flush=True)


def error_groups(binary, parent):
    for mode in ("passthrough", "errno-zero", "errno-stale"):
        for group in ("cache-errors", "io-errors"):
            work, directory, before = prepare_work(parent, f"{mode}-{group}")
            env = dict(os.environ, INSERT_CHECK_MODE=mode)
            result = run([binary, "--case", group, "--work-dir", directory],
                         cwd=work, env=env)
            output = result.stdout.decode()
            assert "2 Tests 0 Failures 0 Ignored" in output, output
            probes = 6 if group == "cache-errors" else 4
            assert f"{probes} cases, {probes} operations (0 inserted, 0 duplicates, " in output
            assert f"{probes} expected errors), 0 live checks, 0 checkpoints" in output
            assert not result.stderr, result.stderr
            assert not list(directory.glob("run-*")), list(directory.iterdir())
            preserved(directory, before)
    print("insert harness: ten real resource probes, stale errno and cleanup: PASS",
          flush=True)


def positive_group(binary, parent):
    for mode in ("passthrough", "errno-zero", "errno-stale"):
        work, directory, before = prepare_work(parent, f"{mode}-empty")
        env = dict(os.environ, INSERT_CHECK_MODE=mode)
        result = run([binary, "--case", "empty", "--work-dir", directory],
                     cwd=work, env=env)
        output = result.stdout.decode()
        assert "2 Tests 0 Failures 0 Ignored" in output, output
        assert ("4 cases, 16 operations (8 inserted, 8 duplicates, 0 expected errors), "
                "30 live checks, 12 checkpoints") in output, output
        assert not result.stderr, result.stderr
        assert not list(directory.glob("run-*")), list(directory.iterdir())
        preserved(directory, before)
    print("insert harness: real inserts/duplicates ignore errno; successful cleanup: PASS",
          flush=True)


def records_from_image(path):
    """Independent native-byte-order decoding for the small retained images."""
    data = path.read_bytes()
    assert data[:4] == b"BPTR"
    flags, size, key_size, value_size = struct.unpack_from("=IIHH", data, 4)
    pointer = "I" if flags & 0x80 else "Q"
    width = struct.calcsize(pointer)
    header_count = struct.unpack_from("=Q", data, 16)[0]
    root = struct.unpack_from("=" + pointer, data, 28)[0]
    index = root
    visited = set()
    while index:
        assert index not in visited
        visited.add(index)
        offset = index * size
        _, level, count = struct.unpack_from("=HHI", data, offset)
        if not level:
            break
        index = struct.unpack_from("=" + pointer, data,
                                   offset + 64 + count * key_size)[0]
    records = []
    visited.clear()
    while index:
        assert index not in visited
        visited.add(index)
        offset = index * size
        node_flags, level, count = struct.unpack_from("=HHI", data, offset)
        assert node_flags & 3 == 3 and level == 0 and count > 0
        keys = offset + 64
        values = keys + count * key_size
        for slot in range(count):
            records.append({"key_hex": data[keys + slot * key_size:
                                            keys + (slot + 1) * key_size].hex(),
                            "value_hex": data[values + slot * value_size:
                                              values + (slot + 1) * value_size].hex()})
        index = struct.unpack_from("=" + pointer, data, offset + 12 + width)[0]
    assert len(records) == header_count, (len(records), header_count)
    return records


def adversary(binary, parent, mode, group="permutations", layout="normal", seed=12345):
    work, directory, before = prepare_work(parent, f"{mode}-{group}-{layout}-{seed}")
    env = dict(os.environ, INSERT_CHECK_MODE=mode)
    result = run([binary, "--case", group, "--layout", layout, "--seed", seed,
                  "--work-dir", directory], cwd=work, env=env, expected=1)
    output = (result.stdout + result.stderr).decode()
    assert "1 Tests 1 Failures 0 Ignored" in output, output
    retained = re.findall(r"insert: retained failed case ([^\n]+)", output)
    assert len(retained) == 1, output
    case = Path(retained[0])
    assert case.parent == directory and case.is_dir(), (case, directory)
    assert list(directory.glob("run-*")) == [case], list(directory.iterdir())
    assert {path.name for path in case.iterdir()} == {
        "instance.bptr", "operations.jsonl", "expected.json", "failure.txt"}
    events = [json.loads(line) for line in (case / "operations.jsonl").read_text().splitlines()]
    expected = json.loads((case / "expected.json").read_text())
    failure = (case / "failure.txt").read_text()
    assert events[0]["seed"] == str(seed)
    assert f"seed={seed}" in failure and "operation " in failure
    assert events[-1]["operation"] == len(events) - 1
    preserved(directory, before)
    return case, events, expected, failure, output


def mutation_checks(binary, parent):
    cases = [
        ("success-noop", "live record count", 1, "permutations"),
        ("wrong-return", "expected return 0; actual 48879", 1, "permutations"),
        ("duplicate-success", "expected return -4; actual 0", 5, "permutations"),
        ("live-count", "live record count", 1, "permutations"),
        ("live-value", "value byte", 1, "permutations"),
        ("caller-write", "modified caller input bytes", 1, "ownership"),
        ("persist-value", "persisted", 8, "permutations"),
    ]
    for layout in ("normal", "lite"):
        for mode, diagnostic, operations, group in cases:
            case, events, expected, failure, output = adversary(
                binary, parent, mode, group, layout)
            assert diagnostic in failure, (mode, failure, output)
            assert len(events) == operations + 1, (mode, events)
            if mode in ("live-count", "live-value"):
                # Unload deliberately repairs these mutants. Thus a checker
                # looking only after flush would miss the already reported bug.
                assert records_from_image(case / "instance.bptr") == expected["records"]
            if mode == "persist-value":
                assert records_from_image(case / "instance.bptr") != expected["records"]
                assert "9 live checks, 0 checkpoints" in output, output
    print("insert harness: return, duplicate, live, ownership and persisted adversaries: PASS",
          flush=True)


def replay_checks(binary, parent):
    sequences = []
    for ordinal, seed in enumerate((12345, 12345, 54321)):
        container = parent / f"replay-{ordinal}"
        container.mkdir()
        _, events, _, _, _ = adversary(binary, container, "success-noop", "binary",
                                       "normal", seed)
        sequences.append(events)
    assert sequences[0] == sequences[1], sequences
    first = [(event.get("key_hex"), event.get("value_hex")) for event in sequences[0][1:]]
    changed = [(event.get("key_hex"), event.get("value_hex")) for event in sequences[2][1:]]
    assert first != changed, (first, changed)
    print("insert harness: identical seeds replay; changed seeds alter attempted records: PASS",
          flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix="insert-harness-") as temporary:
        work = Path(temporary)
        binary = build_proxy(work)
        cli_checks(binary, work)
        positive_group(binary, work)
        error_groups(binary, work)
        mutation_checks(binary, work)
        replay_checks(binary, work)
    print("insert harness integration: PASS", flush=True)


if __name__ == "__main__":
    main()
