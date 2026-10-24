#include "temp_manifest.h"
int main(int argc, char **argv)
{
#ifdef TEMP_MANIFEST_GENERATE
    return temp_manifest_cli(argc, argv, 1);
#else
    return temp_manifest_cli(argc, argv, 0);
#endif
}
