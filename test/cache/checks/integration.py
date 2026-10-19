#!/usr/bin/env python3
"""Exercise cache fixture isolation and disposable assertion/cache mutants."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import tempfile
import re


ROOT = Path(__file__).resolve().parents[3]
CASE_COUNT = 8


def replace_once(source, old, new):
    assert source.count(old) == 1, old
    return source.replace(old, new)


def snapshot(directory):
    return {
        str(path.relative_to(directory)): path.read_bytes() if path.is_file() else None
        for path in directory.rglob("*")
    }


def run(binary, directory, failed_case=None, other_failures=False):
    result = subprocess.run([str(binary)], cwd=directory, capture_output=True,
                            text=True, timeout=10)
    summary = re.search(r"(\d+) Tests (\d+) Failures", result.stdout)
    assert summary and int(summary[1]) == CASE_COUNT, result.stdout
    failures = int(summary[2])
    assert failures >= 1 if failed_case else failures == 0, result.stdout
    if failed_case and not other_failures:
        assert failures == 1, result.stdout
    assert result.returncode == failures, result.stdout + result.stderr
    assert f"{CASE_COUNT} Tests {failures} Failures" in result.stdout, result.stdout
    if failed_case:
        assert f":{failed_case}:FAIL:" in result.stdout, result.stdout
    assert not result.stderr, result.stderr
    return result


def policy_mutants(directory):
    relative = "core/src/bptr_cache.c"
    source = (ROOT / relative).read_text()
    changes = [
        ("fifo-uses-tail", "   uint64_t ret = cache->evict_head;",
         "   uint64_t ret = cache->evict_tail;"),
        ("fifo-unlinked-tail", "   GET_POOL_EN(cache, cache->evict_tail)->evict_next = pool_idx;",
         "   /* old tail was not linked */"),
        ("fifo-hit-keeps-queue-entry", "         if (pool_en->refcnt == 1)",
         "         if (0)"),
    ]
    for name, old, new in changes:
        yield build(directory, name, {relative: replace_once(source, old, new)}), "test_cache_fifo_eviction"


def build(directory, name, sources):
    includes = [ROOT / path for path in
                ("test/cache/src", "test/helper", "core", "core/src", "deps/unity/src")]
    replacements = {}
    for number, (relative, source) in enumerate(sources.items()):
        path = directory / f"{name}-{number}.c"
        obj = directory / f"{name}-{number}.o"
        path.write_text(source)
        subprocess.run(["gcc", "-g", "-DBPTR_TESTING",
                        *("-I" + str(path) for path in includes),
                        "-c", str(path), "-o", str(obj)], check=True,
                       capture_output=True, text=True)
        if relative.startswith("core/"):
            original = ROOT / "obj/core" / (Path(relative).stem + ".o")
        else:
            original = ROOT / "obj/cache/src" / (Path(relative).stem + ".o")
        replacements[original] = obj
    objects = [*sorted((ROOT / "obj/cache/src").glob("*.o")),
               *sorted((ROOT / "obj/core").glob("*.o")), ROOT / "obj/unity.o",
               *sorted((ROOT / "obj/test_util").glob("*.o"))]
    assert replacements.keys() <= set(objects), replacements
    binary = directory / name
    subprocess.run(["gcc", *(str(replacements.get(obj, obj)) for obj in objects),
                    "-o", str(binary)], check=True, capture_output=True, text=True)
    return binary


PROBE = r'''
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
int cache_probe_fd = -1;
static int cache_probe_failures;
static void cache_probe_closed(void)
{
   if (cache_probe_fd < 0) return;
   errno = 0;
   if (fcntl(cache_probe_fd, F_GETFD) != -1 || errno != EBADF)
    {
      fputs("fixture descriptor remained open after teardown\n", stderr);
      cache_probe_failures++;
    }
   cache_probe_fd = -1;
}
'''


def cleanup_mutants(directory):
    main = (ROOT / "test/cache/src/main.c").read_text()
    anchor = "   int persistence_status = cache_persistence_cleanup();"
    main = PROBE + replace_once(main, anchor, anchor + "\n   cache_probe_closed();")
    main = replace_once(main, "   return result;",
                        "   return cache_probe_failures ? 90 : result;")
    targets = [
        ("init", "test/cache/src/cache.c", "test_cache_init",
         "         bptr = cache_fixture_create(temp_it + i);", "bptr"),
        ("lifecycle", "test/cache/src/cache.c", "test_cache_node_lifecycle",
         "\n   bptr = cache_fixture_create(&template);", "bptr"),
        ("capacity", "test/cache/src/cache.c", "test_cache_capacity_boundaries",
         "\n      bptr = cache_fixture_create(&template);", "bptr"),
        ("collision", "test/cache/src/collisions.c", "test_cache_hash_collisions",
         '   TEST_ASSERT_NOT_NULL_MESSAGE(collision_tree, "cannot reopen collision fixture");',
         "collision_tree"),
        ("collision-create", "test/cache/src/collisions.c", "test_cache_hash_collisions",
         '   TEST_ASSERT_NOT_NULL_MESSAGE(collision_tree, "cannot create collision fixture");',
         "collision_tree"),
        ("persistence", "test/cache/src/persistence.c", "test_cache_data_persistence",
         '   TEST_ASSERT_NOT_NULL_MESSAGE(persistence_tree, "cannot reopen persistence fixture");',
         "persistence_tree"),
        ("persistence-create", "test/cache/src/persistence.c", "test_cache_data_persistence",
         '   TEST_ASSERT_NOT_NULL_MESSAGE(persistence_tree, "cannot create persistence fixture");',
         "persistence_tree"),
    ]
    for name, relative, case, anchor, tree in targets:
        source = (ROOT / relative).read_text()
        source = replace_once(source, anchor, anchor +
                              '\n   if (!cache_probe_injected++)\n    {'
                              f'\n      cache_probe_fd = fileno({tree}->file);'
                              '\n      TEST_FAIL_MESSAGE("injected after acquisition");\n    }')
        source = "extern int cache_probe_fd;\nstatic int cache_probe_injected;\n" + source
        yield build(directory, "cleanup-" + name,
                    {relative: source, "test/cache/src/main.c": main}), case

    relative = "test/cache/src/cache.c"
    source = (ROOT / relative).read_text()
    anchor = "   bptr = cache_fixture_try_create(&invalid);"
    source = source.replace(anchor, "   invalid.cache_cap = 2;\n" + anchor +
                            "\n   cache_probe_fd = fileno(bptr->file);", 1)
    source = "extern int cache_probe_fd;\n" + source
    yield build(directory, "cleanup-unexpected-init",
                {relative: source, "test/cache/src/main.c": main}), "test_cache_capacity_boundaries"


def main():
    with tempfile.TemporaryDirectory(prefix="cache-integration-") as temporary:
        directory = Path(temporary)
        work = directory / "work"
        files = work / "bptr_files"
        files.mkdir(parents=True)
        for name in ("cache_lifecycle.bptr", "cache_data_persist.bptr", "lite_DEF_u32_u32.bptr"):
            (files / name).write_bytes(b"caller-prepared fixture: " + name.encode())
        (files / "cache-preserved").mkdir()
        (files / "cache-preserved/sentinel").write_bytes(b"keep caller directories")
        before = snapshot(work)
        binary = ROOT / "bin/cache"
        for _ in range(2):
            run(binary, work)
            assert snapshot(work) == before, "sequential run changed caller files"
        with ThreadPoolExecutor(max_workers=2) as workers:
            list(workers.map(lambda _: run(binary, work), range(2)))
        assert snapshot(work) == before, "parallel runs leaked or changed fixtures"
        print("PASS: repeat runs, concurrent runs and caller fixture preservation")
        for mutant, case in cleanup_mutants(directory):
            run(mutant, work, case)
            assert snapshot(work) == before, mutant.name + " leaked or changed fixtures"
            print("PASS: " + mutant.name + " closes its descriptor and removes owned files")
        for mutant, case in policy_mutants(directory):
            run(mutant, work, case, other_failures=True)
            assert snapshot(work) == before, mutant.name + " leaked or changed fixtures"
            print("PASS: detected " + mutant.name)
        run(binary, work)
        assert snapshot(work) == before, "retry after failures changed caller files"


if __name__ == "__main__":
    main()
