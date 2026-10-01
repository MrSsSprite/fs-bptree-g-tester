#ifndef __TEMP_INTERNAL_H
#define __TEMP_INTERNAL_H

/*----------------------------- Public Includes ------------------------------*/
#include <sys/types.h>
/*--------------------------- Public Includes END ----------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Create every parent directory of @p path
 *
 * Walks @p path and creates each directory prefix it names, tolerating one that
 * already exists (EEXIST).  The last component -- the file itself -- is not
 * created, and a leading '/' is the root directory, which is taken as it is.
 * Shared by the generator, which creates the directory of the template it is
 * about to write, and the instantiator, which creates the directory of the
 * copy.
 *
 * @param[in,out] path  path to create the parents of; modified in place while
 *                      it is walked and restored before returning, also when it
 *                      fails
 * @param[in]     mode  permission bits of a directory that is created
 *
 * @return  a non-negative count of the path separators walked on success; -1 on
 *          failure, with `errno' set (ENOTDIR when a prefix exists but is not a
 *          directory)
 */
long long _ensure_par_dirs(char *path, mode_t mode);
/*--------------------------- Public Functions END ---------------------------*/

#endif
