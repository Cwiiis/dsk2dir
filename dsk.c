
#include "dsk.h"
#include "file.h" // Replace with FatFs header

#ifdef DEBUG
#include <stdio.h>
#endif

#define debug_print(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

int dsk2dir(const char* path)
{
}

int dir2dsk(const char* path)
{
}
