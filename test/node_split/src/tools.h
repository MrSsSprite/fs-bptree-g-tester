#ifndef TOOLS_H
#define TOOLS_H

/*----------------------------- Public Includes ------------------------------*/
#include <stddef.h>
#include <stdint.h>
/*--------------------------- Public Includes END ----------------------------*/


/*------------------------------ Public Defines ------------------------------*/
/* the stand-alone template tools the unit needs, relative to the working
 * directory: it spawns them and never searches `PATH' for them.  The unit does
 * not generate a template -- `bin/temp_gen' is the caller's tool -- so it has
 * no handle on it. */
#define TOOLS_TEMP_INST   "bin/temp_inst"
#define TOOLS_TEMP_VERIFY "bin/temp_verify"

/* `tools_run' reports a child it could not start -- or reap -- as this, with
 * `errno' set to the reason */
#define TOOLS_E_SPAWN (-1)
/*------------------------------ Public Defines END --------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Run a tool and wait for it
 *
 * The child is spawned with `posix_spawn' -- no shell, no `PATH' search: a
 * relative path such as `bin/temp_gen' is resolved against the working
 * directory of the unit -- and reaped with `waitpid', so the caller sees the
 * whole run and nothing of the tool leaks into this process.
 *
 * @param[in] argv  argument vector; `argv[0]' is the tool to run and the last
 *                  entry is NULL
 *
 * @return  the exit status of the tool (0..255), 128 + the signal number when it
 *          was killed, or TOOLS_E_SPAWN when it could not be started or reaped
 *          (`errno' then holds the reason, `posix_spawn' reports it as a return
 *          value)
 */
int tools_run(char *const argv[]);
/**
 * @brief   Copy a template to an instance with `bin/temp_inst'
 *
 * The tool creates the parent directories of @p dst.
 *
 * @param[in] src  template to copy
 * @param[in] dst  destination, left untouched when it already exists
 *
 * @return  the status `tools_run' reports: 0 copied, 1 @p dst exists,
 *          2 usage, 3 copy error.  The tool names the reason on stderr.
 */
int tools_instantiate(const char *src, const char *dst);
/**
 * @brief   Check one image with `bin/temp_verify'
 *
 * The shape of the image is passed explicitly: an instance is named after the
 * position it was split at, so its path says nothing about the layout, and the
 * tool is the one that has to know which lattice and how many levels to expect.
 *
 * @param[in] lay_cnt     levels of the template the image was copied from
 * @param[in] st          first key of the lattice
 * @param[in] interval    distance between two successive keys
 * @param[in] has_new_kv  image holds one inserted record; the tool then expects
 *                        `lay_cnt + 1' levels and tolerates that record
 * @param[in] key         key of the inserted record, when there is one
 * @param[in] val         value of the inserted record, when there is one
 * @param[in] path        image to check
 *
 * @return  the status `tools_run' reports: 0 verified, 1 check failed (the
 *          tool's Unity output on stdout), 2 usage, 3 the image cannot be
 *          loaded
 */
int tools_verify(unsigned int lay_cnt, int64_t st, int64_t interval,
                 _Bool has_new_kv, int64_t key, int64_t val, const char *path);
/**
 * @brief   Name the result of a tool the caller ran
 *
 * @param[out] buf     destination, always NUL terminated
 * @param[in]  size    capacity of @p buf
 * @param[in]  cmd     the tool that was run, as passed to `tools_run'
 * @param[in]  status  what `tools_run' returned
 */
void tools_strstatus(char *buf, size_t size, const char *cmd, int status);
/*--------------------------- Public Functions END ---------------------------*/

#endif
