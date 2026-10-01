/*----------------------------- Private Includes -----------------------------*/
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "temp_full.h"
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Macros ------------------------------*/
/* Process status of the tool, part of its interface: 0 the copy was created, 1
 * the destination already exists and was left untouched, 2 the command line
 * cannot be served, and 3 the copy failed -- its reason is printed on stderr. */
#define INST_EXIT_OK     (0)
#define INST_EXIT_EXISTS (1)
#define INST_EXIT_USAGE  (2)
#define INST_EXIT_FAIL   (3)
/*------------------------------ Private Macros END --------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static void usage(FILE *stream, const char *prog);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------------- MAIN -----------------------------------*/
int main(int argc, char **argv)
{
   const char *src, *dst;
   int status;

   if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
    { usage(stdout, argv[0]); return INST_EXIT_OK; }
   if (argc != 3)
    { usage(stderr, argv[0]); return INST_EXIT_USAGE; }

   src = argv[1];
   dst = argv[2];
   status = temp_instantiate(dst, src);
   switch (status)
    {
   case 0  : printf("temp_inst: %s: copied\n", dst);
             return INST_EXIT_OK;
   case 1  : fprintf(stderr,
                     "temp_inst: %s: already exists, left untouched\n", dst);
             return INST_EXIT_EXISTS;
   default : /* -1: the failing call left its reason in `errno' */
             fprintf(stderr, "temp_inst: %s -> %s: %s\n", src, dst,
                     errno != 0 ? strerror(errno) : "cannot be copied");
             return INST_EXIT_FAIL;
    }
}
/*--------------------------------- MAIN END ---------------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static void usage(FILE *stream, const char *prog)
{
   fprintf(stream,
           "usage: %s SRC DST\n"
           "\n"
           "Copy the template SRC to DST, creating the parent directories of\n"
           "DST.  An existing DST is never overwritten.\n"
           "\n"
           "  -h, --help      print this help and exit\n"
           "\n"
           "Exit status: 0 copied, 1 DST exists, 2 the command line cannot be\n"
           "served, 3 the copy failed.\n",
           prog);
}
/*-------------------------- Private Functions END ---------------------------*/
