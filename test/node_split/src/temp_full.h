#ifndef __TEMP_FULL_H
#define __TEMP_FULL_H

/*----------------------------- Public Includes ------------------------------*/
#include <stdint.h>
/*--------------------------- Public Includes END ----------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int temp_full_generate(unsigned int lay_cnt, int64_t st, int64_t interval,
                       _Bool is_lite, uint32_t node_size);
int temp_instantiate(const char *path, const char *temp);
/*--------------------------- Public Functions END ---------------------------*/

#endif
