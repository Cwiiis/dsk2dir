
#include "dsk.h"
#include "file.h" // Replace with FatFs header
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
#include <stdio.h>
#endif

#define ARRAY_LENGTH(array) (sizeof((array))/sizeof((array)[0]))
#define debug_print(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt "\n", __VA_ARGS__); } while (0)

int dsk2dir(const TCHAR* path)
{
#define read_and_validate(fd, buf, br, size) \
  if(f_read(&(fd), (buf), (size), &(br)) != FR_OK || \
     (br) < (size)) { retval = DSK_FILE_ERROR; goto dsk2dirend; }

    int retval = 0;
    BYTE buffer[16];

    FIL fd;
    if (f_open(&fd, path, FA_READ) != FR_OK)
        return -1;

    // Read dsk type from header
    UINT br;
    read_and_validate(fd, buffer, br, 8);

    // Check disk format
    BYTE is_extended = 0;
    if (strncmp((char*)buffer, "MV - CPC", 8) != 0) {
        if (strncmp((char*)buffer, "EXTENDED", 8) != 0) {
            retval = DSK_UNKNOWN_FORMAT;
            goto dsk2dirend;
        }
        is_extended = 1;
    }

    // Skip the rest of the type and creator header info
    f_lseek(&fd, 0x30);

    // Read in track/side info
    BYTE tracks, sides;
    read_and_validate(fd, &tracks, br, 1);
    read_and_validate(fd, &sides, br, 1);

    // Read track offsets
    BYTE track_offsets[0x100 - 0x34];
    if (is_extended && tracks * sides >= ARRAY_LENGTH(track_offsets)) {
        retval = DSK_TOO_LARGE;
        goto dsk2dirend;
    }
    if (is_extended)
        f_lseek(&fd, 2);
    read_and_validate(fd, track_offsets, br, is_extended ? ARRAY_LENGTH(track_offsets) : 2);

    BYTE track = 0, side = 0;
    FSIZE_T track_adr = 0x100;

    do {
        if (track >= tracks)
            break;

        // Skip to first track information block
        f_lseek(&fd, track_adr);

        // Validate track info header
        read_and_validate(fd, buffer, br, 12);
        if (strncmp((char*)buffer, "Track-Info\r\n", 12) != 0) {
            retval = DSK_MALFORMED_TRACK_INFO;
            goto dsk2dirend;
        }

        // Skip 4 unused bytes
        read_and_validate(fd, buffer, br, 4);

        // Verify track and side number
        read_and_validate(fd, buffer, br, 2);
        if (buffer[0] != track) {
            retval = DSK_UNEXPECTED_TRACK;
            goto dsk2dirend;
        }
        if (buffer[1] != side) {
            retval = DSK_UNEXPECTED_SIDE;
            goto dsk2dirend;
        }

        // Skip 2 unused bytes
        read_and_validate(fd, buffer, br, 2);

        // Read sector size and number of sectors
        BYTE sector_size;
        BYTE n_sectors;
        read_and_validate(fd, &sector_size, br, 1);
        read_and_validate(fd, &n_sectors, br, 1);

        // Skip to first sector (past gap length, filler byte and sector info)
        f_lseek(&fd, track_adr + 0x100);

        // Read directory listing
        if (track_adr == 0x100) do {
            read_and_validate(fd, buffer, br, 1);
            if (buffer[0] != 0xE5) {
                // Read filename
                read_and_validate(fd, buffer, br, 11);
                debug_print("Filename: %.8s.%.3s", buffer, &buffer[8]);
                // Read record number
                read_and_validate(fd, buffer, br, 3);
                debug_print("Record no.: %u", ((unsigned)buffer[0] << 16) | ((unsigned)buffer[1] << 8) | buffer[2]);
                // Read extent
                read_and_validate(fd, buffer, br, 1);
                debug_print("Extent: %hhu", buffer[0]);
                // Read block numbers
                // Note, for disks < 256k, these are 8-bit, for >= 256k, 16-bit
                read_and_validate(fd, buffer, br, 16);
            } else
                break;
        } while(1);

        // Increment track indices
        if (side < sides) {
            ++side;
        } else {
            side = 0;
            ++track;
        }

        if (is_extended)
            track_adr += ((FSIZE_T)track_offsets[(track * sides) + side]) << 8;
        else
            track_adr += (((FSIZE_T)track_offsets[0]) << 8) | track_offsets[1];
    } while(1);

dsk2dirend:
    return (f_close(&fd) == FR_OK) ? retval : -1;

#undef read_and_validate
}

int dir2dsk(const TCHAR* path)
{
    return -1;
}
