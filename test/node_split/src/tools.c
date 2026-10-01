/*----------------------------- Private Includes -----------------------------*/
#include "tools.h"
#include <errno.h>
#include <inttypes.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
/*--------------------------- Private Includes END ---------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
/* the environment the tools inherit: `posix_spawn' has no default */
extern char **environ;
/*------------------------- Forward Declarations END -------------------------*/


/*---------------------------- Private Variables -----------------------------*/
/* `errno' of the last failed `tools_run'.  `tools_strstatus' is called by the
 * code that reports the failure, which may run in between; keep the reason the
 * spawn failed with instead of whatever `errno' holds by then. */
static int tools_errno;
/*---------------------------- Private Variables END --------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int tools_run(char *const argv[])
{
   pid_t pid;
   int status, rc;

   rc = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
   if (rc != 0)
    {
      /* `posix_spawn' reports the failure as a return value, not in `errno' */
      tools_errno = rc;
      errno = rc;
      return TOOLS_E_SPAWN;
    }

   if (waitpid(pid, &status, 0) == -1)
    {
      tools_errno = errno;
      return TOOLS_E_SPAWN;
    }

   if (WIFEXITED(status)) return WEXITSTATUS(status);
   if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);

   /* `waitpid' without `WUNTRACED' does not report a stopped child */
   tools_errno = ECHILD;
   return TOOLS_E_SPAWN;
}


int tools_generate(const char *dir, unsigned int lay_cnt, int64_t st,
                   int64_t interval, _Bool is_lite, uint32_t node_size)
{
   char lay[16], first[32], step[32], size[16];
   char *argv[11];
   size_t n = 0;

   snprintf(lay, sizeof lay, "%u", lay_cnt);
   snprintf(first, sizeof first, "%" PRIi64, st);
   snprintf(step, sizeof step, "%" PRIi64, interval);
   snprintf(size, sizeof size, "%u", node_size);

   argv[n++] = (char *)TOOLS_TEMP_GEN;
   argv[n++] = (char *)"--dir";
   argv[n++] = (char *)dir;
   if (!is_lite) argv[n++] = (char *)"--norm";
   /* the layout is spelled out rather than left to the tool's defaults: the
    * table is what the templates of this unit are, the tool only builds them */
   argv[n++] = (char *)"--node-size";
   argv[n++] = size;
   argv[n++] = lay;
   argv[n++] = first;
   argv[n++] = step;
   argv[n] = NULL;

   return tools_run(argv);
}


int tools_instantiate(const char *src, const char *dst)
{
   char *argv[4];

   argv[0] = (char *)TOOLS_TEMP_INST;
   argv[1] = (char *)src;
   argv[2] = (char *)dst;
   argv[3] = NULL;

   return tools_run(argv);
}


int tools_verify(unsigned int lay_cnt, int64_t st, int64_t interval,
                 _Bool has_new_kv, int64_t key, int64_t val, const char *path)
{
   char lay[16], first[32], step[32], k[32], v[32];
   char *argv[14];
   size_t n = 0;

   snprintf(lay, sizeof lay, "%u", lay_cnt);
   snprintf(first, sizeof first, "%" PRIi64, st);
   snprintf(step, sizeof step, "%" PRIi64, interval);

   argv[n++] = (char *)TOOLS_TEMP_VERIFY;
   argv[n++] = (char *)"--lay-cnt";
   argv[n++] = lay;
   argv[n++] = (char *)"--st";
   argv[n++] = first;
   argv[n++] = (char *)"--interval";
   argv[n++] = step;
   if (has_new_kv)
    {
      snprintf(k, sizeof k, "%" PRIi64, key);
      snprintf(v, sizeof v, "%" PRIi64, val);
      argv[n++] = (char *)"--key";
      argv[n++] = k;
      argv[n++] = (char *)"--val";
      argv[n++] = v;
    }
   argv[n++] = (char *)path;
   argv[n] = NULL;

   return tools_run(argv);
}


void tools_strstatus(char *buf, size_t size, const char *cmd, int status)
{
   if (buf == NULL || size == 0) return;

   if (status == TOOLS_E_SPAWN)
      snprintf(buf, size, "cannot run %s: %s (build the tools with 'make')",
               cmd, strerror(tools_errno));
   else if (status >= 128)
      snprintf(buf, size, "%s: killed by signal %d", cmd, status - 128);
   else
      snprintf(buf, size, "%s: exited with status %d", cmd, status);
}
/*--------------------------- Public Functions END ---------------------------*/
