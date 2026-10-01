/*----------------------------- Private Includes -----------------------------*/
#include "temp_full.h"
#include "temp_internal.h"
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
/*--------------------------- Private Includes END ---------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static int _copy_file(const char *dst, const char *src);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int temp_instantiate(const char *dst, const char *src)
{
   char path[PATH_MAX];
   int len = snprintf(path, sizeof path, "%s", dst);

   if (len < 0 || (size_t)len >= sizeof path)
    { errno = ENAMETOOLONG; return -1; }
   if (_ensure_par_dirs(path, 0755) == -1) return -1;

   return _copy_file(path, src);
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
/**
 * @brief   Copy @p src to the not yet existing @p dst
 *
 * The destination is created `O_EXCL', so an existing file is never written to:
 * it is reported as status 1 and left untouched.  A copy that fails once the
 * destination exists is removed before returning, so that a retry cannot
 * mistake a truncated image for a complete one.
 *
 * @param[in] dst  destination to create
 * @param[in] src  image to copy
 *
 * @return  0 when @p src was copied, 1 when @p dst already exists, and -1 when
 *          the copy failed, with `errno' set to the reason of the failing call
 */
static int _copy_file(const char *dst, const char *src)
{
   int sfd = -1, dfd = -1, saved;
   struct stat st;
   char buf[4096];
   ssize_t n;

   if (access(dst, F_OK) == 0) return 1;

   sfd = open(src, O_RDONLY);
   if (sfd == -1) return -1;
   if (fstat(sfd, &st) == -1) goto COPY_ERR;

   dfd = open(dst, O_WRONLY | O_CREAT | O_EXCL, st.st_mode & 0777);
   if (dfd == -1)
    {
      /* created between the check above and here: same answer as finding it
       * there in the first place */
      if (errno == EEXIST)
       { close(sfd); return 1; }
      goto COPY_ERR;
    }

   while ((n = read(sfd, buf, sizeof(buf))) > 0)
    {
      char *p = buf;
      ssize_t written = 0;
      /* `written' must be the byte count `write' reports, not a boolean: with
       * the parentheses around the comparison the whole remaining buffer is
       * written at every step while the cursor advances by one byte, so the
       * destination grows by n + (n - 1) + ... bytes and ends with a garbage
       * tail of the same bytes rewritten. */
      while (n > 0 && (written = write(dfd, p, n)) > 0)
       { p += written; n -= written; }
      if (written < 0 || n != 0)
       {
         /* a write of 0 is not a reason by itself */
         if (written >= 0) errno = EIO;
         goto COPY_ERR;
       }
    }
   if (n == -1) goto COPY_ERR;
   if (close(dfd) == -1)
    { dfd = -1; goto COPY_ERR; }
   dfd = -1;
   close(sfd);
   return 0;

   /*-------------------------- Error Handling Zone --------------------------*/
COPY_ERR:
   saved = errno;
   if (dfd != -1) close(dfd);
   if (sfd != -1) close(sfd);
   /* the image is incomplete: drop it, so that a retry does not find a
    * destination it would refuse to overwrite */
   remove(dst);
   errno = saved;
   return -1;
}
/*-------------------------- Private Functions END ---------------------------*/
