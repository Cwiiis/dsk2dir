
#pragma once

enum DSK_ERROR_CODES {
    DSK_FILE_ERROR = -1,
    DSK_OK = 0,
    DSK_UNKNOWN_FORMAT = 1,
    DSK_TOO_LARGE = 2,
    DSK_MALFORMED_TRACK_INFO = 3,
    DSK_UNEXPECTED_TRACK = 4,
    DSK_UNEXPECTED_SIDE = 5
};

int dsk2dir(const char* path);
int dir2dsk(const char* path);
