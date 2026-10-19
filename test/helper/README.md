# Shared test helpers

The files here support runnable units in sibling directories under `test/`.
The helper modules remain flat because each has a distinct name and only a
source/header pair:

| Module | Purpose |
| --- | --- |
| `progress.{c,h}` | Live terminal progress, log verdicts and line termination. |
| `test_bptr_setup.{c,h}` | Fixture paths and Unity-asserting tree creation. |
| `test_bptr_temp.{c,h}` | Tree parameter matrices used by cache and flush tests. |
| `test_util.h` | Umbrella header for all three helpers. |

Include a specific header, such as `progress.h`, or include `test_util.h`.
`config.mk` adds this directory to the include path and compiles its C sources
into `obj/test_util/`, which is linked through `PREREQ_OBJS`. Standalone checks
can link an individual helper object when they do not need the core or the
other helpers.

There is no runnable unit or Makefile here. Root `make` discovers test units
through `test/*/Makefile`, so it skips this directory.
