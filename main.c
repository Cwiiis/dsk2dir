
#include "dsk.h"

int main(int argc, char** argv)
{
    if (argc < 2) return 1;

    dsk2dir(argv[1]);

    return 0;
}
