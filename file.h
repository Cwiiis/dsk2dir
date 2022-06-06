
#pragma once

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

enum FAMODEFLAGS {
    FA_READ = 1,
    FA_WRITE = 2,
    FA_OPEN_EXISTING = 4,
    FA_CREATE_NEW = 8,
    FA_CREATE_ALWAYS = 16,
    FA_OPEN_ALWAYS = 32,
    FA_OPEN_APPEND = 64
};

typedef FILE* FIL;
typedef char TCHAR;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t UINT;
typedef size_t FSIZE_T;

enum FAATTRIBFLAGS {
    AM_RDO = 1,
    AM_HID = 2,
    AM_SYS = 4,
    AM_ARC = 8,
    AM_DIR = 16
};

typedef struct {
    FSIZE_T fsize;
    WORD fdate;
    WORD ftime;
    BYTE fattrib;
    TCHAR fname[12 + 1];  // Ignoring FF_USE_LFN
} FILINFO;

FRESULT f_open(FIL*, const TCHAR* path, BYTE mode);
FRESULT f_close(FIL*);
FRESULT f_read(FIL*, void* buff, UINT btr, UINT* br);
FRESULT f_lseek(FIL*, FSIZE_T);
FRESULT f_write(FIL*, const void* buff, UINT btw, UINT* bw);
FRESULT f_opendir(DIR*, const TCHAR* path);
FRESULT f_closedir(DIR*);
FRESULT f_readdir(DIR*, FILINFO*);
#define f_rewinddir(dp) f_readdir((dp), 0)
