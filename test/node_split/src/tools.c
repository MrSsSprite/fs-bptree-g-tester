/*----------------------------- Private Includes -----------------------------*/
#include "tools.h"
#include <errno.h>
#include <inttypes.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Macros ------------------------------*/
/* How much of a tool's output is kept to be replayed when it fails.  A tool
 * reports a defect in a few hundred bytes; the cap is what keeps a run-away tool
 * from filling the memory of the unit.  What does not fit is counted, and read
 * away all the same. */
#define TOOLS_CAPTURE_MAX (64u * 1024u)
/*------------------------------ Private Macros END --------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
/* the environment the tools inherit: `posix_spawn' has no default */
extern char **environ;

static char *_capture(int fd, size_t *out_len, size_t *dropped);
static void _replay(const char *cmd, const char *buf, size_t len,
                    size_t dropped);
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
   posix_spawn_file_actions_t fa;
   char *buf;
   size_t len = 0, dropped = 0;
   int fds[2], rc, status;
   pid_t pid;

   if (pipe(fds) != 0)
    {
      tools_errno = errno;
      return TOOLS_E_SPAWN;
    }

   /* Both streams of the tool end in the pipe: nothing the tool prints can
    * land in the unit's own log, and everything it printed is there to be
    * replayed when the run has to be reported as failed. */
   rc = posix_spawn_file_actions_init(&fa);
   if (rc != 0)
    {
      /* an uninitialized action list must not be built on */
      close(fds[0]);
      close(fds[1]);
      tools_errno = rc;
      errno = rc;
      return TOOLS_E_SPAWN;
    }
   posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
   posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
   posix_spawn_file_actions_addclose(&fa, fds[0]);
   posix_spawn_file_actions_addclose(&fa, fds[1]);
   rc = posix_spawn(&pid, argv[0], &fa, NULL, argv, environ);
   posix_spawn_file_actions_destroy(&fa);
   /* the parent reads: its own copy of the write end has to go, or the drain
    * below never sees the tool close it */
   close(fds[1]);

   if (rc != 0)
    {
      close(fds[0]);
      /* `posix_spawn' reports the failure as a return value, not in `errno' */
      tools_errno = rc;
      errno = rc;
      return TOOLS_E_SPAWN;
    }

   /* The output is read to its end before the child is reaped: a tool that
    * prints more than a pipe holds must never block on it. */
   buf = _capture(fds[0], &len, &dropped);
   close(fds[0]);

   if (waitpid(pid, &status, 0) == -1)
    {
      tools_errno = errno;
      free(buf);
      return TOOLS_E_SPAWN;
    }

   if (WIFEXITED(status)) rc = WEXITSTATUS(status);
   else if (WIFSIGNALED(status)) rc = 128 + WTERMSIG(status);
   else
    {
      /* `waitpid' without `WUNTRACED' does not report a stopped child */
      tools_errno = ECHILD;
      free(buf);
      return TOOLS_E_SPAWN;
    }

   /* A tool that did its job says nothing in the log of the unit: its report is
    * the unit's own line for it.  One that did not is quoted verbatim, so the
    * defect it found is not thrown away with the noise around it. */
   if (rc != 0) _replay(argv[0], buf, len, dropped);
   free(buf);

   return rc;
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


/*---------------------------- Private Functions ----------------------------*/
/**
 * @brief   Read a tool's output to its end
 *
 * Reads until the write end of the pipe is closed -- which the tool does by
 * exiting -- so that a tool printing more than the pipe holds is never blocked
 * on it.  At most `TOOLS_CAPTURE_MAX' bytes are kept; the rest is drained and
 * counted.
 *
 * @param[in]  fd        read end of the pipe
 * @param[out] out_len   bytes kept in the returned buffer
 * @param[out] dropped   bytes read after the buffer was full
 *
 * @return  the captured output, NUL terminated and owned by the caller, or NULL
 *          when none could be kept (the output is then only counted)
 */
static char *_capture(int fd, size_t *out_len, size_t *dropped)
{
   char scratch[4096];
   char *buf = malloc(TOOLS_CAPTURE_MAX + 1);
   size_t len = 0;

   *out_len = 0;
   *dropped = 0;

   for (;;)
    {
      ssize_t got;
      /* whatever room is left: none once the buffer is full, or from the
       * start when it could not be allocated at all */
      size_t room = buf == NULL ? 0 : TOOLS_CAPTURE_MAX - len;

      if (room == 0)
       {
         /* keep reading, or the tool blocks on a pipe nobody empties */
         got = read(fd, scratch, sizeof scratch);
         if (got > 0)
          {
            *dropped += (size_t)got;
            continue;
          }
       }
      else
       {
         got = read(fd, buf + len, room);
         if (got > 0)
          {
            len += (size_t)got;
            continue;
          }
       }

      if (got < 0 && errno == EINTR) continue;
      /* end of file, or a read that failed: keep what has been read, the
       * status of the tool is the verdict, not the capture */
      break;
    }

   if (buf != NULL)
    {
      buf[len] = '\0';
      *out_len = len;
    }

   return buf;
}


/**
 * @brief   Quote a failed tool's output on stderr
 *
 * @param[in] cmd      the tool that was run, as passed to `tools_run'
 * @param[in] buf      captured output, when there is any
 * @param[in] len      bytes in @p buf
 * @param[in] dropped  bytes of output that did not fit in the capture
 */
static void _replay(const char *cmd, const char *buf, size_t len,
                    size_t dropped)
{
   /* the unit's line for this tool is on stdout: flush it, or it lands in the
    * log after the report it is there to introduce */
   fflush(stdout);

   if (buf != NULL && len != 0)
    {
      fwrite(buf, 1, len, stderr);
      if (buf[len - 1] != '\n') fputc('\n', stderr);
    }
   if (dropped != 0)
      fprintf(stderr, "tools: %s: %zu more bytes of output were dropped\n",
              cmd, dropped);
}
/*-------------------------- Private Functions END ---------------------------*/
