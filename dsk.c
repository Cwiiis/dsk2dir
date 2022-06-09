
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

static int verify_track(FIL* fd, BYTE* buffer, BYTE track)
{
    UINT br;

    // Read and verify track information header
    if (f_read(fd, buffer, 12, &br) != FR_OK || br < 12) return DSK_MALFORMED_TRACK_INFO;
    if (strncmp((char*)buffer, "Track-Info\r\n", 12) != 0) return DSK_MALFORMED_TRACK_INFO;

    // Skip unused bytes
    if (f_read(fd, buffer, 4, &br) != FR_OK || br < 4) return DSK_MALFORMED_TRACK_INFO;

    // Verify expected track and side numbers
    if (f_read(fd, buffer, 1, &br) != FR_OK || br < 1 || buffer[0] != track) return DSK_UNEXPECTED_TRACK;
    if (f_read(fd, buffer, 1, &br) != FR_OK || br < 1 || buffer[0] != 0) return DSK_UNEXPECTED_SIDE;

    // Skip unused bytes
    if (f_read(fd, buffer, 2, &br) != FR_OK || br < 2) return DSK_MALFORMED_TRACK_INFO;

    // Verify sector size
    if (f_read(fd, buffer, 1, &br) != FR_OK || br < 1 || buffer[0] != 2) return DSK_UNEXPECTED_SECTOR_SIZE;

    // Verify number of sectors
    if (f_read(fd, buffer, 1, &br) != FR_OK || br < 1 || buffer[0] != 9) return DSK_UNEXPECTED_SECTOR_NUMBER;

    return DSK_OK;
}

static inline void get_block_indices(BYTE is_system, unsigned block, BYTE* track, BYTE* sector)
{
    *track = (block * 2) / 9;
    *sector = (block * 2) % 9;
    if (is_system) *track += 2;
}

static inline FSIZE_T get_track_adr(BYTE* track_offsets, BYTE is_extended, BYTE tracks, BYTE track)
{
    if (track >= tracks)
        return 0;

    if (is_extended) {
        FSIZE_T adr = 0x100;
        for (unsigned i = 0; i < track; ++i)
            adr += ((FSIZE_T)track_offsets[i]) << 8;
        return adr;
    }

    return 0x100 + (((FSIZE_T)track_offsets[0] << 8) | track_offsets[1]) * track;
}

static int read_filename(FIL* fd, BYTE* buffer, WORD* attr)
{
    UINT br, i;

    // Read name
    if (f_read(fd, buffer, 8, &br) != FR_OK || br < 8) return DSK_FILE_ERROR;

    // Separate out file attributes
    *attr = 0;
    for (i = 0; i < 8; ++i) {
        *attr |= ((buffer[i] & 0x80) >> 7) << (10 - i);
        buffer[i] &= 0x7F;
    }
    BYTE name_len;
    for (name_len = 8; name_len > 0 && (buffer[name_len - 1] == 0x20 || buffer[name_len - 1] == '\0'); --name_len);

    // Return early if name is null
    if (name_len == 0) {
        buffer[0] = '\0';
        return DSK_OK;
    }

    // Read extension
    if (f_read(fd, &buffer[name_len + 1], 3, &br) != FR_OK || br < 3) return DSK_FILE_ERROR;

    // Separate out file attributes
    for (i = name_len + 1; i < name_len + 1 + 3; ++i) {
        *attr |= ((buffer[i] & 0x80) >> 7) << (name_len + 3 - i);
        buffer[i] &= 0x7F;
    }

    BYTE ext_len;
    for (ext_len = 3; ext_len > 0 && (buffer[name_len + ext_len] == 0x20 || buffer[name_len + ext_len] == '\0'); --ext_len);

    // Terminate string
    if (ext_len) {
        buffer[name_len] = '.';
        buffer[name_len + ext_len + 1] = '\0';
    } else
        buffer[name_len] = '\0';

    return DSK_OK;
}

int dsk2dir(const TCHAR* path)
{
#define read_and_validate(fd, buf, br, size) \
  if(f_read(&(fd), (buf), (size), &(br)) != FR_OK || \
     (br) < (size)) { retval = DSK_FILE_ERROR; goto dsk2dirend; }

    int retval = DSK_OK;
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
    BYTE tracks;
    read_and_validate(fd, &tracks, br, 1);

    // TODO: Support multi-sided disks
    read_and_validate(fd, buffer, br, 1);
    if (buffer[0] != 1) {
        debug_print("Unsupported number of tracks: %hhu", buffer[0]);
        retval = DSK_UNSUPPORTED_FORMAT;
        goto dsk2dirend;
    }

    // Read track offsets
    BYTE track_offsets[0x100 - 0x34];
    if (is_extended && tracks >= ARRAY_LENGTH(track_offsets)) {
        retval = DSK_TOO_LARGE;
        goto dsk2dirend;
    }
    if (is_extended)
        f_lseek(&fd, 0x34);
    read_and_validate(fd, track_offsets, br, is_extended ? tracks : 2);

    // Check if this is a system or data disk by reading the first sector ID
    f_lseek(&fd, 0x11A);
    read_and_validate(fd, buffer, br, 1);
    if (buffer[0] != 0x41 && buffer[0] != 0xC1) {
        debug_print("Unsupported disk format (first sector: %hhu)", buffer[0]);
        retval = DSK_UNSUPPORTED_FORMAT;
        goto dsk2dirend;
    }
    BYTE is_system = (buffer[0] == 0x41);

    // Read directory listing
    BYTE dir_track = is_system ? 2 : 0;
    for (unsigned i = 0; i < 64; ++i) {
        FSIZE_T track_adr = get_track_adr(track_offsets, is_extended, tracks, dir_track);

        // Verify track header is correct
        f_lseek(&fd, track_adr);
        if ((retval = verify_track(&fd, buffer, dir_track)) != DSK_OK) goto dsk2dirend;

        // Skip to directory entry (past gap length, filler byte and sector info)
        f_lseek(&fd, track_adr + 0x100 + (i * 32));

        // Check user
        read_and_validate(fd, buffer, br, 1);
        if (buffer[0] != 0) continue;

        // Read filename and attributes
        WORD attr;
        if ((retval = read_filename(&fd, buffer, &attr)) != DSK_OK) goto dsk2dirend;
        if (buffer[0] == '\0') continue;
        debug_print("Filename: %s", buffer);
        debug_print("Attributes: %hu", attr);
        // Read extent and record number
        read_and_validate(fd, buffer, br, 4);
        debug_print("Extent: %u", ((unsigned)buffer[2] << 5) | (buffer[0] & 0x1F));
        debug_print("Record count: %hhu", buffer[3]);

        // Start writing out file
    }

dsk2dirend:
    return (f_close(&fd) == FR_OK) ? retval : -1;

#undef read_and_validate
}

int dir2dsk(const TCHAR* path)
{
    return -1;
}
