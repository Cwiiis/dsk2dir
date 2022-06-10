
#include "dsk.h"
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2) return 1;

    switch (dsk2dir(argv[1])) {
    case DSK_FILE_ERROR :
        fprintf(stderr, "DSK_FILE_ERROR\n");
        break;
    case DSK_OK :
        fprintf(stderr, "DSK_OK\n");
        break;
    case DSK_UNKNOWN_FORMAT :
        fprintf(stderr, "DSK_UNKNOWN_FORMAT\n");
        break;
    case DSK_UNSUPPORTED_FORMAT :
        fprintf(stderr, "DSK_UNSUPPORTED_FORMAT\n");
        break;
    case DSK_TOO_LARGE :
        fprintf(stderr, "DSK_TOO_LARGE\n");
        break;
    case DSK_MALFORMED_TRACK_INFO :
        fprintf(stderr, "DSK_MALFORMED_TRACK_INFO\n");
        break;
    case DSK_UNEXPECTED_TRACK :
        fprintf(stderr, "DSK_UNEXPECTED_TRACK\n");
        break;
    case DSK_UNEXPECTED_SIDE :
        fprintf(stderr, "DSK_UNEXPECTED_SIDE\n");
        break;
    case DSK_UNEXPECTED_SECTOR_ID :
        fprintf(stderr, "DSK_UNEXPECTED_SECTOR_ID\n");
        break;
    case DSK_UNEXPECTED_SECTOR_SIZE :
        fprintf(stderr, "DSK_UNEXPECTED_SECTOR_SIZE\n");
        break;
    case DSK_UNEXPECTED_SECTOR_NUMBER :
        fprintf(stderr, "DSK_UNEXPECTED_SECTOR_NUMBER\n");
        break;
    }

    return 0;
}
