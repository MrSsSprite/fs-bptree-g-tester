"""Run real tools against isolated fixtures; leave existing inputs untouched.

Run from the repository root after building: python3 test/node_split/checks/integration.py
"""
import inspect
import json
import os
from pathlib import Path
import shutil
import struct
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


def fixture(directory, height, size=112, normal=False, interval=5, start=-11,
            node_keys=()):
    argv = [BIN / "temp_gen", "--dir", directory, "--node-size", size]
    if normal:
        argv.append("--norm")
    for override in node_keys:
        argv.extend(("--node-keys", override))
    run([*argv, height, start, interval])


def assert_success(result, templates, custom=False):
    output = result.stdout.decode()
    assert not result.stderr, result.stderr.decode()
    assert "\r" not in output
    if not custom:
        assert "skip " not in output
    assert "2 Tests 0 Failures 0 Ignored" in output
    assert output.count(":PASS") == 2
    assert output.count("Tests 0 Failures") == 1
    assert output.count(".bptr: PASS") == templates * 2


def read_image(path):
    """Decode the disk layout independently of the runner and verifier."""
    data = Path(path).read_bytes()
    normal = not (struct.unpack_from("=I", data, 4)[0] & 0x80)
    pointer = "Q" if normal else "I"
    size = struct.unpack_from("=I", data, 8)[0]
    root, _, _, count = struct.unpack_from("=" + pointer * 4, data, 28)
    image = {"root": root, "nodes": count, "leaves": 0,
             "records": struct.unpack_from("=Q", data, 16)[0],
             "height": struct.unpack_from("=I", data, 24)[0],
             "branch_max": (size - 64 - (8 if normal else 4)) //
                           (16 if normal else 12),
             "leaf_max": (size - 64) // 16, "by_index": {}, "paths": {}}

    def visit(index, path=()):
        offset = index * size
        _, level, keys = struct.unpack_from("=HHI", data, offset)
        values = offset + 64 + 8 * keys
        node = {"index": index, "count": keys, "level": level,
                "keys": list(struct.unpack_from("=" + "q" * keys, data,
                                                offset + 64)),
                "children": list(struct.unpack_from("=" + pointer * (keys + 1),
                                                    data, values)) if level else []}
        image["by_index"][str(index)] = node
        image["paths"][".".join(map(str, path))] = node
        if level:
            for child, descendant in enumerate(node["children"]):
                visit(descendant, (*path, child))
        else:
            image["leaves"] += 1

    visit(root)
    assert len(image["by_index"]) == count
    return image


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
        expected_height = (int(args[args.index("--expect-height") + 1])
                           if "--expect-height" in args else
                           int(args[args.index("--lay-cnt") + 1]) + 1)
        assert struct.unpack_from("=I", header, 24)[0] == expected_height
image = (read_image(args[-1]) if tool == "temp_verify" and
         "--key" in args and "--node-keys" in args else None)
if image:
    image.pop("paths")
    for node in image["by_index"].values():
        node.pop("keys")
        node.pop("children")
result = subprocess.run([str(Path(os.environ["SPLIT_CHECK_BIN"]) / tool), *args])
status = result.returncode
if is_instance:
    if tool == "temp_verify" and os.environ.get("SPLIT_CHECK_FAIL_VERIFY"):
        if Path(args[-1]).name.endswith("-2.bptr"):
            print("injected verifier failure", file=sys.stderr)
            status = 1
    with open(os.environ["SPLIT_CHECK_TRACE"], "a") as trace:
        trace.write(json.dumps({"tool": tool, "path": args[-1], "status": status,
                                "args": args, "image": image}) + "\n")
sys.exit(status)
'''


def wrapped_tools(work):
    tools = work / "bin"
    tools.mkdir(parents=True)
    for name in ("temp_inst", "temp_verify"):
        wrapper = tools / name
        wrapper.write_text(f"#!{sys.executable}\n" +
                           "from pathlib import Path\nimport struct\n" +
                           inspect.getsource(read_image) + WRAPPER)
        wrapper.chmod(0o755)


def tool_environment(work, failure=False):
    trace = work / "events.jsonl"
    env = dict(os.environ, SPLIT_CHECK_BIN=str(BIN), SPLIT_CHECK_TRACE=str(trace))
    if failure:
        env["SPLIT_CHECK_FAIL_VERIFY"] = "1"
    else:
        env.pop("SPLIT_CHECK_FAIL_VERIFY", None)
    return trace, env


def read_events(trace):
    events = [json.loads(line) for line in trace.read_text().splitlines()]
    assert len(events) % 2 == 0
    for copy, verify in zip(events[::2], events[1::2]):
        assert copy["tool"] == "temp_inst" and copy["status"] == 0
        assert verify["tool"] == "temp_verify" and copy["path"] == verify["path"]
    return events


def instrumented(parent, failure=False, custom=False):
    work = parent / (("custom-" if custom else "") +
                     ("failure" if failure else "ordering"))
    wrapped_tools(work)
    templates = work / "templates"
    fixture(templates, 4, size=128 if custom else 112,
            node_keys=("0=4",) if custom else ())
    trace, env = tool_environment(work, failure)
    result = subprocess.run([str(BIN / "node_split"), str(templates)], cwd=work,
                            env=env, capture_output=True)
    events = read_events(trace)
    files = list((work / "bptr_files/temp/split").glob("*.bptr"))
    if failure:
        assert result.returncode == 1 and len(events) == 6
        assert len(files) == 1 and files[0].name == "4-2.bptr"
        assert events[-1]["status"] == 1
        assert b"injected verifier failure" in result.stderr
        assert b"failed to verify the instance" in result.stdout
        assert b"verify bptr_files/temp/split/4-2.bptr:" in result.stdout
        assert b"4-*.bptr: PASS" not in result.stdout
    else:
        assert result.returncode == 0
        assert_success(result, 1, custom=custom)
        assert not files
        if not custom:
            assert len(events) == 2 * 67
    print(f"real tools: {'custom ' if custom else ''}"
          f"{'failure retention' if failure else 'one-instance ordering'}: PASS",
          flush=True)


def options(args, flag):
    return [args[index + 1] for index, arg in enumerate(args) if arg == flag]


def original_overrides(path):
    name = Path(path).name
    return name.split("--keys-", 1)[1][:-5].split(",") if "--keys-" in name else []


def chosen_leaf(original, key):
    node = original["by_index"][str(original["root"])]
    route = []
    while node["level"]:
        child = sum(separator <= key for separator in node["keys"])
        route.append((node, child))
        node = original["by_index"][str(node["children"][child])]
    return node, route


def check_custom_sample(original, overrides, verify, fixed=None):
    """Derive a second expectation from disk; fixed scenarios also use constants."""
    args = verify["args"]
    assert options(args, "--node-keys") == overrides, args
    key = int(options(args, "--key")[0])
    leaf, route = chosen_leaf(original, key)
    assert leaf["count"] == original["leaf_max"], (key, leaf)
    assert key not in leaf["keys"]
    assert int(options(args, "--val")[0]) == key * 2
    nodes = original["nodes"] + 1
    absorbing = None
    for index in reversed(range(len(route))):
        ancestor, _ = route[index]
        if ancestor["count"] < original["branch_max"]:
            absorbing = index
            break
        nodes += 1
    height = original["height"]
    if absorbing is None:
        height += 1
        nodes += 1
    expected = (height, nodes, original["leaves"] + 1, original["records"] + 1)
    if fixed is not None:
        assert expected == fixed, (key, expected, fixed)
        expected = fixed
    supplied = tuple(int(options(args, flag)[0]) for flag in
                     ("--expect-height", "--expect-nodes", "--expect-leaves"))
    assert supplied == expected[:3], (key, supplied, expected)
    image = verify["image"]
    actual = tuple(image[field] for field in ("height", "nodes", "leaves", "records"))
    assert actual == expected, (key, actual, expected)
    if absorbing is not None:
        assert image["root"] == original["root"]
        for index, (ancestor, _) in enumerate(route[:absorbing + 1]):
            assert image["by_index"][str(ancestor["index"])]["count"] == (
                ancestor["count"] + (index == absorbing))
    else:
        assert image["root"] != original["root"]
    return leaf, route, key, absorbing


def stopped_cascade_cases(parent):
    # These totals are deliberately independent of the runner's shape arithmetic.
    for normal, shape_count, totals in (
        (False, 4, (((2, 7, 6, 21), None),
                    ((3, 43, 36, 141), (4, 46, 36, 141)),
                    ((4, 254, 211, 841), (5, 257, 211, 841)))),
        (True, 2, (((2, 5, 4, 13), None),
                   ((3, 21, 16, 61), (4, 24, 16, 61)),
                   ((4, 82, 61, 241), (5, 85, 61, 241)))),
    ):
        for height, (stopped, full) in zip((2, 3, 4), totals):
            name = f"{'normal' if normal else 'lite'}-stop-{height}"
            work = parent / name
            wrapped_tools(work)
            directory = work / "templates"
            override = f"{'root' if height == 2 else '0'}={shape_count}"
            fixture(directory, height, size=128, normal=normal, start=0,
                    interval=16, node_keys=(override,))
            original = read_image(next(directory.glob("*.bptr")))
            trace, env = tool_environment(work)
            result = run([BIN / "node_split", directory], cwd=work, env=env)
            assert_success(result, 1, custom=True)
            events = read_events(trace)
            leaf_roles, separator_positions, promotion_roles = set(), set(), set()
            stopped_count = full_count = 0
            for event in events[1::2]:
                key = int(options(event["args"], "--key")[0])
                _, route = chosen_leaf(original, key)
                inside = height == 2 or route[0][1] == 0
                leaf, route, key, absorbing = check_custom_sample(
                    original, [override], event, stopped if inside else full)
                if inside:
                    stopped_count += 1
                    leaf_roles.add(sum(record < key for record in leaf["keys"]))
                    separator_positions.add(route[absorbing][1])
                    for branch, child in route[absorbing + 1:]:
                        boundary = (original["branch_max"] + 1) // 2
                        promotion_roles.add((child > boundary) - (child < boundary))
                else:
                    full_count += 1
            assert stopped_count and (height == 2 or full_count)
            assert {2, 3, 4} <= leaf_roles, leaf_roles
            assert {0, shape_count // 2, shape_count} <= separator_positions
            if height == 4:
                assert promotion_roles == {-1, 0, 1}, promotion_roles
            assert not list((work / "bptr_files/temp/split").glob("*.bptr"))
            print(f"{name}, fixed stopped/full cascade totals and roles: PASS", flush=True)


def custom_shapes(parent):
    work = parent / "custom-shapes"
    wrapped_tools(work)
    directory = work / "templates"
    # Generation order deliberately disagrees with the loader's canonical order.
    for height, overrides in (
        (4, ("3.2=3", "1=3", "1.0=2", "0.1.2=2")),
        (2, ("root=4", "0=2", "1=3")),
        (3, ("0.1=2", "root=3", "0=4")),
        (2, ()),
    ):
        fixture(directory, height, size=128, start=0, interval=16, node_keys=overrides)
    expected_order = sorted(path.name for path in directory.glob("*.bptr"))
    originals = {path.name: read_image(path) for path in directory.glob("*.bptr")}
    sequences = []
    for _ in range(2):
        trace, env = tool_environment(work)
        trace.unlink(missing_ok=True)
        result = run([BIN / "node_split", directory], cwd=work, env=env)
        assert_success(result, 4, custom=True)
        assert b"non-full leaves" in result.stdout
        events = read_events(trace)
        order, sequence, sampled = [], [], {}
        for copy, verify in zip(events[::2], events[1::2]):
            source = Path(copy["args"][0]).name
            if not order or order[-1] != source:
                order.append(source)
            overrides = original_overrides(source)
            if overrides:
                leaf, route, key, _ = check_custom_sample(originals[source], overrides, verify)
                sampled.setdefault(source, []).append(
                    (leaf["index"], tuple(child for _, child in route), (key + 8) // 16))
            else:
                assert not options(verify["args"], "--node-keys")
                assert not options(verify["args"], "--expect-height")
            sequence.append((source, verify["args"]))
        assert order == expected_order, order
        for source, selected in sampled.items():
            original = originals[source]
            selected_ids = {index for index, _, _ in selected}
            leaves = [(tuple(map(int, path.split("."))) if path else (), node)
                      for path, node in original["paths"].items() if not node["level"]]
            full_leaves = [(path, node) for path, node in leaves
                           if node["count"] == original["leaf_max"]]
            for override in original_overrides(source):
                selector = override.split("=", 1)[0]
                prefix = tuple(map(int, selector.split("."))) if selector != "root" else ()
                if any(path[:len(prefix)] == prefix for path, _ in full_leaves):
                    assert any(path[:len(prefix)] == prefix for _, path, _ in selected), (
                        source, selector, selected)
            for index, (_, leaf) in enumerate(leaves):
                if leaf["count"] == original["leaf_max"]:
                    continue
                for neighbors in (reversed(leaves[:index]), iter(leaves[index + 1:])):
                    neighbor = next((node for _, node in neighbors
                                     if node["count"] == original["leaf_max"]), None)
                    if neighbor is not None:
                        assert neighbor["index"] in selected_ids, (source, leaf, neighbor)
            if original["height"] <= 2:
                offset, expected = 0, set()
                for _, leaf in leaves:
                    if leaf["count"] == original["leaf_max"]:
                        expected.update(range(offset + 1, offset + leaf["count"] + 1))
                        if offset == 0:
                            expected.add(0)
                    offset += leaf["count"]
                assert {gap for _, _, gap in selected} == expected
        sequences.append(sequence)
    assert sequences[0] == sequences[1]
    assert not list((work / "bptr_files/temp/split").glob("*.bptr"))
    print("asymmetric/nested shapes, partial leaves, ordering and repeatability: PASS",
          flush=True)

    for name, size, height, overrides in (
        ("all-partial", 128, 2, ("root=1", "0=2", "1=2")),
        ("partial-root-leaf", 128, 1, ("root=1",)),
        ("custom-single-key", 92, 4, ("root=1",)),
    ):
        case = parent / name
        wrapped_tools(case)
        directory = case / "templates"
        fixture(directory, height, size=size, start=0, interval=16, node_keys=overrides)
        trace, env = tool_environment(case)
        result = run([BIN / "node_split", directory], cwd=case, env=env)
        assert b"2 Tests 0 Failures 0 Ignored" in result.stdout
        assert not result.stderr
        if name.startswith("all-") or name == "partial-root-leaf":
            assert b"no full leaves to split" in result.stdout
            assert not trace.exists()
            assert not (case / "bptr_files/temp/split").exists()
        else:
            original = read_image(next(directory.glob("*.bptr")))
            for verify in read_events(trace)[1::2]:
                leaf, route, key, _ = check_custom_sample(original, list(overrides), verify)
                # With one-key leaves, a gap at a separator selects its preceding leaf.
                assert key > leaf["keys"][0] or all(child == 0 for _, child in route)
            assert_success(result, 1, custom=True)
        print(f"{name}: PASS", flush=True)


def invalid_names(parent):
    source_dir = parent / "canonical-source"
    fixture(source_dir, 3, size=128, start=0, interval=16,
            node_keys=("root=3", "0=4"))
    source = next(source_dir.glob("*.bptr"))
    for index, name in enumerate((
        "03-0-16--keys-root=3,0=4.bptr",
        "3-0-16--keys-0=4,root=3.bptr",
        "3-0-16--keys-root=03,0=4.bptr",
        "3-0-16--keys-root=3,0=4,00=4.bptr",
        "3-0-16--keys-root=3,0..1=2.bptr",
    )):
        work = parent / f"invalid-name-{index}"
        directory = work / "templates"
        directory.mkdir(parents=True)
        shutil.copyfile(source, directory / name)
        result = subprocess.run([str(BIN / "node_split"), str(directory)],
                                cwd=work, capture_output=True)
        assert result.returncode == 1
        assert b"canonical" in result.stderr
        assert not (work / "bptr_files/temp/split").exists()
    print("noncanonical and malformed fixture names: PASS", flush=True)


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
        stopped_cascade_cases(work)
        custom_shapes(work)
        invalid_names(work)
        instrumented(work, custom=True)
        instrumented(work, failure=True, custom=True)


if __name__ == "__main__":
    main()
