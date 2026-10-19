"""Exercise search with real tools, isolated inputs and fault injection.

Run from the repository root after building search and temp:
    python3 test/search/checks/integration.py
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
BIN = ROOT / "bin"
INT64_MIN, INT64_MAX = -(1 << 63), (1 << 63) - 1


def run(argv, cwd=ROOT, env=None, expected=0):
    result = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env,
                            capture_output=True)
    assert result.returncode == expected, (
        argv, result.returncode, result.stdout.decode(), result.stderr.decode())
    return result


def fixture(directory, height=2, size=112, normal=False, start=-11,
            interval=16, node_keys=()):
    args = [BIN / "temp_gen", "--dir", directory, "--node-size", size]
    if normal:
        args.append("--norm")
    for override in node_keys:
        args.extend(("--node-keys", override))
    run([*args, height, start, interval])


def snapshots(directory):
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(directory.glob("*.bptr"))}


def read_records(path):
    """Read the persisted leaf layer without calling the library or verifier."""
    data = path.read_bytes()
    lite = bool(struct.unpack_from("=I", data, 4)[0] & 0x80)
    pointer = "I" if lite else "Q"
    size = struct.unpack_from("=I", data, 8)[0]
    records = struct.unpack_from("=Q", data, 16)[0]
    root = struct.unpack_from("=" + pointer, data, 28)[0]
    index = root
    while True:
        _, level, count = struct.unpack_from("=HHI", data, index * size)
        if not level:
            break
        index = struct.unpack_from("=" + pointer, data,
                                   index * size + 64 + 8 * count)[0]
    keys, leaves, seen = [], [], set()
    while index:
        assert index not in seen, (path, index)
        seen.add(index)
        offset = index * size
        _, level, count = struct.unpack_from("=HHI", data, offset)
        assert level == 0 and count > 0
        leaf_keys = list(struct.unpack_from("=" + "q" * count, data, offset + 64))
        values = struct.unpack_from("=" + "q" * count, data,
                                    offset + 64 + 8 * count)
        assert list(values) == [key * 2 for key in leaf_keys]
        leaves.append((index, len(keys), count))
        keys.extend(leaf_keys)
        # Node metadata: flags, level, count, checksum, parent, next, previous.
        index = struct.unpack_from("=" + pointer, data,
                                   offset + 12 + struct.calcsize(pointer))[0]
    assert len(keys) == records and keys == sorted(set(keys))
    return keys, leaves


def miss_count(keys):
    queries = {INT64_MIN, INT64_MAX}
    if keys[0] > INT64_MIN:
        queries.add(keys[0] - 1)
    if keys[-1] < INT64_MAX:
        queries.add(keys[-1] + 1)
    for left, right in zip(keys, keys[1:]):
        if right - left > 1:
            queries.update((left + 1, right - 1))
    return len(queries.difference(keys))


# Wrappers see every copy and verification, including their exact arguments.
# Before a new copy they require that the previous successful instance has
# already disappeared; images intentionally retained after a failure are exempt.
WRAPPER = r'''
import json
import os
from pathlib import Path
import subprocess
import sys

tool, args = Path(sys.argv[0]).name, sys.argv[1:]
trace = Path(os.environ["SEARCH_CHECK_TRACE"])
events = ([json.loads(line) for line in trace.read_text().splitlines()]
          if trace.exists() else [])
retained = {event["path"] for event in events
            if event["tool"] == "temp_verify" and event["status"] != 0}
directory = Path("bptr_files/temp/search")
if tool == "temp_inst":
    assert {str(path) for path in directory.rglob("*.bptr")} <= retained
else:
    assert args[-1].startswith("bptr_files/temp/search/run-"), args
    assert Path(args[-1]).name == "instance.bptr", args
    assert Path(args[-1]).is_file(), args
status = subprocess.run([str(Path(os.environ["SEARCH_CHECK_BIN"]) / tool),
                         *args]).returncode
if tool == "temp_verify":
    ordinal = sum(event["tool"] == tool for event in events) + 1
    if ordinal == int(os.environ.get("SEARCH_CHECK_FAIL_VERIFY", "0")):
        print("injected search verifier failure", file=sys.stderr)
        status = 1
with trace.open("a") as output:
    output.write(json.dumps({"tool": tool, "args": args, "path": args[-1],
                             "status": status}) + "\n")
sys.exit(status)
'''


def wrapped_tools(work, fail_verify=0, missing=None):
    tools = work / "bin"
    tools.mkdir(parents=True)
    for name in ("temp_inst", "temp_verify"):
        if name == missing:
            continue
        wrapper = tools / name
        wrapper.write_text(f"#!{sys.executable}\n" + WRAPPER)
        wrapper.chmod(0o755)
    trace = work / "events.jsonl"
    env = dict(os.environ, SEARCH_CHECK_BIN=str(BIN), SEARCH_CHECK_TRACE=str(trace),
               SEARCH_CHECK_FAIL_VERIFY=str(fail_verify))
    return trace, env


def events_from(trace):
    return ([json.loads(line) for line in trace.read_text().splitlines()]
            if trace.exists() else [])


def assert_clean(work):
    directory = work / "bptr_files/temp/search"
    assert not list(directory.glob("run-*")), list(directory.glob("run-*"))


def assert_success(result, directory):
    templates = sorted(directory.glob("*.bptr"))
    output = result.stdout.decode()
    assert not result.stderr, result.stderr.decode()
    assert "\r" not in output
    cases = 1 + 5 * len(templates)
    assert f"{cases} Tests 0 Failures 0 Ignored" in output, output
    assert output.count(":PASS") == cases, output
    assert len(re.findall(r"\d+ Tests \d+ Failures \d+ Ignored", output)) == 1
    counts = {}
    for phase in ("hits", "misses"):
        matches = re.findall(rf"^    {phase} (.+): PASS \((\d+) queries\)$",
                             output, re.MULTILINE)
        assert len(matches) == len(templates), (phase, output)
        counts[phase] = {Path(name).name: int(count) for name, count in matches}
    for path in templates:
        keys, _ = read_records(path)
        assert counts["hits"][path.name] == len(keys), (path, counts)
        assert counts["misses"][path.name] == miss_count(keys), (path, counts)
        for phase in ("ownership", "cache failures", "io failures"):
            assert re.search(rf"^    {phase} .*{re.escape(path.name)}: PASS "
                             r"\(\d+ scenarios\)$", output, re.MULTILINE), output
    return sum(counts["hits"].values()) + sum(counts["misses"].values())


def successful_matrix(parent):
    total_queries = total_templates = 0
    # Separate directories are necessary: layout and size are not in file names.
    matrix = (
        ("lite-full", False, 512, (1, 2, 3), -11, 16, ()),
        ("normal-full", True, 512, (1, 2, 3), 1004, 2, ()),
        ("lite-tall", False, 128, (4,), -107, 2, ()),
        ("normal-tall", True, 112, (5, 8), -101, 16, ()),
        ("lite-interval-one", False, 112, (1, 3), -3, 1, ()),
        ("normal-interval-one", True, 112, (3,), 9, 1, ()),
        ("lite-uneven", False, 128, (3,), -301, 16,
         ("root=3", "0=2", "0.1=2", "1.0=3")),
        ("normal-uneven", True, 128, (3,), 17, 2,
         ("root=1", "0=1", "0.0=2", "1.0=3")),
        ("lite-one-key", False, 92, (1, 4, 8), -9, 16, ("root=1",)),
        ("normal-one-key", True, 104, (1, 4), 3, 2, ("root=1",)),
        ("normal-one-key-leaves", True, 104, (2,), -9, 16,
         ("root=1", "0=1", "1=1")),
        ("value-min", False, 92, (1,), INT64_MIN // 2, 1, ()),
        ("value-max", True, 104, (1,), INT64_MAX // 2 - 1, 1, ()),
    )
    for name, normal, size, heights, start, interval, overrides in matrix:
        work = parent / name
        trace, env = wrapped_tools(work)
        directory = work / "templates"
        for height in heights:
            fixture(directory, height, size, normal, start, interval, overrides)
        before = snapshots(directory)
        result = run([BIN / "search", directory], cwd=work, env=env)
        total_queries += assert_success(result, directory)
        assert snapshots(directory) == before, name
        assert_clean(work)
        events = events_from(trace)
        assert events and all(event["status"] == 0 for event in events), events
        # Only disposable copies may reach the verifier, even the input guard.
        assert all(event["path"].startswith("bptr_files/temp/search/run-")
                   for event in events)
        assert all(Path(event["args"][0]).parent == directory
                   for event in events if event["tool"] == "temp_inst")
        total_templates += len(before)
        print(f"{name}: {len(before)} templates, exhaustive query counts, "
              "copy lifecycle and immutable inputs: PASS", flush=True)
    print(f"matrix: {total_templates} templates, {total_queries} routing queries: PASS",
          flush=True)


def directory_interface(parent):
    for name in ("missing", "empty"):
        work = parent / name
        work.mkdir()
        directory = work / "templates"
        if name == "empty":
            directory.mkdir()
            (directory / "ignored.txt").write_text("not a fixture\n")
        result = run([BIN / "search", directory], cwd=work)
        assert b"warning: nothing to test:" in result.stderr
        assert b"Tests" not in result.stdout and b":PASS" not in result.stdout
        assert not (work / "bptr_files").exists()
    work = parent / "default"
    trace, env = wrapped_tools(work)
    directory = work / "bptr_files/temp/full"
    fixture(directory, height=1)
    before = snapshots(directory)
    result = run([BIN / "search"], cwd=work, env=env)
    assert b"default bptr_files/temp/full/" in result.stdout
    assert_success(result, directory)
    assert snapshots(directory) == before
    assert events_from(trace)
    assert_clean(work)
    print("default, empty and missing directory interfaces: PASS", flush=True)


def malformed_names(parent):
    source_dir = parent / "name-source"
    fixture(source_dir, height=3, size=128,
            node_keys=("root=3", "0=4"))
    source = next(source_dir.glob("*.bptr"))
    for index, name in enumerate((
        "bad.bptr", "03--11-16.bptr", "0--11-16.bptr",
        "3--11-16--keys-0=4,root=3.bptr",
        "3--11-16--keys-root=03,0=4.bptr",
        "3--11-16--keys-root=3,0..1=2.bptr",
        "3--11-16--keys-root=3,00=4.bptr",
    )):
        work = parent / f"bad-name-{index}"
        directory = work / "templates"
        directory.mkdir(parents=True)
        shutil.copyfile(source, directory / name)
        before = snapshots(directory)
        result = run([BIN / "search", directory], cwd=work, expected=1)
        assert name.encode() in result.stderr
        assert b"Tests" not in result.stdout
        assert snapshots(directory) == before
        assert not (work / "bptr_files").exists()
    print("malformed full and custom names rejected before cases: PASS", flush=True)


def tool_failures(parent):
    for missing in ("temp_inst", "temp_verify"):
        work = parent / f"missing-{missing}"
        _, env = wrapped_tools(work, missing=missing)
        directory = work / "templates"
        fixture(directory, height=1)
        before = snapshots(directory)
        result = subprocess.run([str(BIN / "search"), str(directory)], cwd=work,
                                env=env, capture_output=True)
        assert result.returncode > 0, result.stdout.decode()
        assert missing.encode() in result.stdout + result.stderr
        assert snapshots(directory) == before
    for ordinal, phase in ((1, "guard"), (2, "final")):
        work = parent / f"failed-{phase}-verify"
        trace, env = wrapped_tools(work, fail_verify=ordinal)
        directory = work / "templates"
        fixture(directory, height=1)
        before = snapshots(directory)
        result = run([BIN / "search", directory], cwd=work, env=env, expected=1)
        assert b"injected search verifier failure" in result.stderr
        retained = re.findall(rb"search: retained failed instance ([^\n]+)",
                              result.stderr + result.stdout)
        assert len(retained) == 1, (result.stdout, result.stderr)
        failed = work / retained[0].decode()
        assert failed.is_file() and failed.name == "instance.bptr"
        files = list((work / "bptr_files/temp/search").rglob("*.bptr"))
        assert files == [failed], files
        events = events_from(trace)
        failures = [event for event in events if event["status"]]
        assert len(failures) == 1 and failures[0]["path"] == retained[0].decode()
        failed_events = [event for event in events if event["path"] == retained[0].decode()]
        assert len(failed_events) == ordinal + 1, failed_events
        assert failed_events[-1]["tool"] == "temp_verify"
        assert snapshots(directory) == before
    print("missing tools, guard/final verifier failure and retained copies: PASS",
          flush=True)


def wrong_result(parent):
    """Build a throwaway index mutant to prove query diagnostics and cleanup."""
    work = parent / "wrong-result"
    _, env = wrapped_tools(work)
    original = (ROOT / "core/src/bptr_core.c").read_text()
    needle = "(struct bptr_find_res){ is_found, node, srch_res }"
    assert original.count(needle) == 1, "update the disposable search mutant"
    mutant = work / "mutant_core.c"
    mutant.write_text(original.replace('#include "../bptree.h"',
                                      f'#include "{ROOT / "core/bptree.h"}"')
                     .replace(needle,
                              "(struct bptr_find_res){ is_found, node, srch_res + 1 }"))
    sources = [*sorted((ROOT / "test/search/src").glob("*.c")),
               *sorted((ROOT / "test/helper").glob("*.c")),
               *sorted((ROOT / "core/src").glob("*.c")),
               ROOT / "deps/unity/src/unity.c"]
    sources = [mutant if source == ROOT / "core/src/bptr_core.c" else source
               for source in sources]
    # temp_shape is shared naming support and deliberately not a library API.
    sources.append(ROOT / "utils/temp/src/temp_shape.c")
    binary = work / "search-mutant"
    includes = [ROOT / "core", ROOT / "core/src", ROOT / "test/helper",
                ROOT / "deps/unity/src", ROOT / "utils/temp/src"]
    run([os.environ.get("CC", "cc"), "-Wall", "-Wextra", "-g",
         *[f"-I{path}" for path in includes], *sources, "-o", binary])
    directory = work / "templates"
    fixture(directory, height=1)
    before = snapshots(directory)
    result = subprocess.run([str(binary), str(directory)], cwd=work, env=env,
                            capture_output=True)
    assert result.returncode > 0
    output = (result.stdout + result.stderr).decode()
    assert re.search(r"query -?\d+: expected leaf \d+ index \d+ found [01]; "
                     r"actual leaf \d+ index \d+ found [01]", output), output
    assert next(directory.glob("*.bptr")).name in output
    retained = re.findall(r"search: retained failed instance ([^\n]+)", output)
    assert retained and all((work / path).is_file() for path in retained)
    assert snapshots(directory) == before
    print("disposable wrong-index mutant: query diagnostics and retention: PASS",
          flush=True)


def main():
    for name in ("search", "temp_gen", "temp_inst", "temp_verify"):
        assert (BIN / name).is_file(), f"build first: make search temp ({name} missing)"
    with tempfile.TemporaryDirectory(prefix="search-integration-") as temp:
        work = Path(temp)
        successful_matrix(work)
        directory_interface(work)
        malformed_names(work)
        tool_failures(work)
        wrong_result(work)
    print("search integration: PASS", flush=True)


if __name__ == "__main__":
    main()
