
#define _DEFAULT_SOURCE

#include "file.h"
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_LENGTH(array) (sizeof((array))/sizeof((array)[0]))

FRESULT f_open(FIL* fd, const TCHAR* path, BYTE mode)
{
    char *mode_string = NULL;
    if (mode & FA_READ) {
        mode &= ~FA_READ;
        if (mode & FA_WRITE) {
            mode &= ~FA_WRITE;
            if (mode & FA_CREATE_ALWAYS) {
                mode &= ~FA_CREATE_ALWAYS;
                mode_string = "w+";
            } else if (mode & FA_OPEN_APPEND) {
                mode &= ~FA_OPEN_APPEND;
                mode_string = "a+";
            } else if (mode & FA_CREATE_NEW) {
                mode &= ~FA_CREATE_NEW;
                mode_string = "w+x";
            } else
                mode_string = "r+";
        } else
            mode_string = "r";
    } else if (mode & FA_WRITE) {
        mode &= ~FA_WRITE;
        if (mode & FA_CREATE_ALWAYS) {
            mode &= ~FA_CREATE_ALWAYS;
            mode_string = "w";
        } else if (mode & FA_OPEN_APPEND) {
            mode &= ~FA_OPEN_APPEND;
            mode_string = "a";
        } else if (mode & FA_CREATE_NEW) {
            mode &= ~FA_CREATE_NEW;
            mode_string = "wx";
        }
        // FIXME: Docs imply you can't use write flag alone, verify.
    }

    if (!mode_string || mode)
        return FR_INVALID_PARAMETER;

    if ((*fd = fopen(path, mode_string)))
        return FR_OK;

    // FIXME: Handle a few more of these cases
    switch(errno) {
    case EACCES:
        return FR_DENIED;
    case EBUSY:
        return FR_LOCKED;
    case EEXIST:
        return FR_EXIST;
    case EINVAL:
        return FR_INVALID_PARAMETER;
    case ENOENT:
        return FR_NO_FILE;
    case ENOMEM:
        return FR_NOT_ENOUGH_CORE;
    default:
        fprintf(stderr, "fopen failed with errno %d\n", errno);
        return FR_INT_ERR;
    }
}

FRESULT f_close(FIL* fd)
{
    if (fclose(*fd) == 0)
        return FR_OK;
    switch(errno) {
    case EBADF:
        return FR_INVALID_OBJECT;
    case EIO:
        return FR_DISK_ERR;
    default:
        return FR_INT_ERR;
    }
}

FRESULT f_read(FIL* fd, void* buff, UINT btr, UINT* br)
{
    size_t ret = fread(buff, 1, btr, *fd);
    if (ret == btr || feof(*fd)) {
        *br = ret;
        return FR_OK;
    }

    // FIXME: How do you use ferror... Does it set errno?
    return FR_DISK_ERR;
}

FRESULT f_lseek(FIL* fd, FSIZE_T offset)
{
    if (fseek(*fd, offset, SEEK_SET) == 0)
        return FR_OK;
    switch(errno) {
    case EINVAL:
        return FR_INVALID_OBJECT;
    default:
    case ESPIPE:
        return FR_INT_ERR;
    }
}

FRESULT f_write(FIL* fd, const void* buff, UINT btw, UINT* bw)
{
    size_t ret = fwrite(buff, 1, btw, *fd);
    if (ret == btw) {
        *bw = ret;
        return FR_OK;
    }

    // FIXME: See f_read note.
    return FR_DISK_ERR;
}

FRESULT f_opendir(DIR* dir, const TCHAR* path)
{
    dir = opendir(path);
    if (dir)
        return FR_OK;

    switch(errno) {
    default:
    case EACCES:
        return FR_DENIED;
    case EMFILE:
    case ENFILE:
        return FR_TOO_MANY_OPEN_FILES;
    case ENOENT:
        return FR_NO_PATH;
    case ENOMEM:
        return FR_NOT_ENOUGH_CORE;
    case ENOTDIR:
        // FIXME: What does FatFs actually do for this?
        return FR_NO_PATH;
    }
}

FRESULT f_closedir(DIR* dir)
{
    if (closedir(dir))
        return FR_INVALID_OBJECT;
    return FR_OK;
}

FRESULT f_readdir(DIR* dir, FILINFO* info)
{
    if (!info) {
        rewinddir(dir);
        return FR_OK;
    }

    errno = 0;
    struct dirent* dirinfo = readdir(dir);
    if (!dirinfo) {
        switch(errno) {
        case 0:
            info->fname[0] = '\0';
            return FR_OK;
        case EBADF:
            return FR_INVALID_OBJECT;
        default:
            return FR_INT_ERR;
        }
    }

    if (strlen(dirinfo->d_name) > ARRAY_LENGTH(info->fname))
        return FR_INT_ERR;

    int fd = dirfd(dir);
    if (fd == -1)
        return FR_INT_ERR;

    struct stat fileinfo;
    if (fstatat(fd, dirinfo->d_name, &fileinfo, 0) == -1) {
        // FIXME: Translate some of the valid error codes?
        return FR_INT_ERR;
    }

    struct tm* timeinfo = localtime(&fileinfo.st_mtim.tv_sec);
    if (!timeinfo)
        return FR_INT_ERR;

    info->fsize = fileinfo.st_size;
    info->fdate = (MIN(2107, MAX(1980, timeinfo->tm_year - 80)) << 9) |
        ((timeinfo->tm_mon + 1) << 5) | timeinfo->tm_mday;
    info->ftime = (timeinfo->tm_hour << 11) | (timeinfo->tm_min << 5) |
        (timeinfo->tm_sec / 2);
    info->fattrib = 0;
    if (info->fname[0] == '.')
        info->fattrib |= AM_HID;
    if (faccessat(fd, dirinfo->d_name, W_OK, 0) != 0)
        info->fattrib |= AM_RDO;
    if (dirinfo->d_type & DT_DIR)
        info->fattrib |= AM_DIR;
    // FIXME: Should we set AM_SYS/AM_ARC somehow?
    strcpy(info->fname, dirinfo->d_name);

    return FR_OK;
}
