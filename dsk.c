
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

    // Skip past reserved system tracks
    BYTE track = is_system ? 2 : 0;

    for (; track < tracks; ++track) {
        FSIZE_T track_adr = get_track_adr(track_offsets, is_extended, tracks, track);
        debug_print("Reading track %hhu at address %lx", track, track_adr);
        f_lseek(&fd, track_adr);

        if ((retval = verify_track(&fd, buffer, track)) != DSK_OK) goto dsk2dirend;

        // Skip to first sector (past gap length, filler byte and sector info)
        f_lseek(&fd, track_adr + 0x100);

        // Iterate over directory listing
        // FIXME: Is there a limit to the number of entries that we should validate...?
        BYTE entry = 0;
        do {
            f_lseek(&fd, track_adr + 0x100 + ((FSIZE_T)entry) * 32);
            read_and_validate(fd, buffer, br, 1);
            if (buffer[0] == 0) {
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

                // Start writing out file
            } else if (buffer[0] == 0xE5)
                goto dsk2dirend;
            ++entry;
        } while(1);
    }

dsk2dirend:
    return (f_close(&fd) == FR_OK) ? retval : -1;

#undef read_and_validate
}

int dir2dsk(const TCHAR* path)
{
    return -1;
}
