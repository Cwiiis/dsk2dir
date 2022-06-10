
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
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
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

static inline FSIZE_T get_track_adr(BYTE* track_offsets, BYTE is_extended, BYTE track)
{
    if (is_extended) {
        FSIZE_T adr = 0x100;
        for (unsigned i = 0; i < track; ++i)
            adr += ((FSIZE_T)track_offsets[i]) << 8;
        return adr;
    }

    return 0x100 + (((FSIZE_T)track_offsets[0] << 8) | track_offsets[1]) * track;
}

static inline int get_sector_adr(FIL* fd, BYTE* buffer, BYTE* track_offsets, BYTE is_extended, BYTE is_system, BYTE track, BYTE sector, FSIZE_T* adr)
{
    int dsk_error;
    UINT br;

    *adr = get_track_adr(track_offsets, is_extended, track);

    // Verify track header is correct
    if (f_lseek(fd, *adr) != FR_OK) return DSK_FILE_ERROR;
    if ((dsk_error = verify_track(fd, buffer, track)) != DSK_OK) return dsk_error;

    for (unsigned i = 0; i < 9; ++i) {
        if (f_lseek(fd, (*adr) + 0x1A + (8 * i)) != FR_OK) return DSK_FILE_ERROR;
        BYTE sector_id;
        if (f_read(fd, &sector_id, 1, &br) != FR_OK || br < 1) return DSK_FILE_ERROR;
        sector_id -= (is_system ? 0x41 : 0xC1);

        // Validate sector ID
        if (sector_id >= 9) return DSK_UNEXPECTED_SECTOR_ID;

        // Validate sector size
        BYTE sector_size;
        if (f_read(fd, &sector_size, 1, &br) != FR_OK || br < 1 || sector_size != 2) return DSK_UNEXPECTED_SECTOR_SIZE;

        buffer[sector_id] = i;
    }

    *adr += 0x100 + (buffer[sector] * 512);
    return DSK_OK;
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

static inline void increment_sector(BYTE* track, BYTE* sector)
{
    if (*sector == 8) {
        ++(*track);
        *sector = 0;
    } else
        ++(*sector);
}

int dsk2dir(const TCHAR* path)
{
#define read_and_validate(fd, buf, br, size) \
  if(f_read(&(fd), (buf), (size), &(br)) != FR_OK || \
     (br) < (size)) { retval = DSK_FILE_ERROR; goto dsk2dirend; }

    unsigned i;
    int retval = DSK_OK;
    BYTE buffer[16];

    FIL fd;
    if (f_open(&fd, path, FA_READ) != FR_OK)
        return DSK_FILE_ERROR;

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
    BYTE n_tracks;
    read_and_validate(fd, &n_tracks, br, 1);

    // TODO: Support multi-sided disks
    read_and_validate(fd, buffer, br, 1);
    if (buffer[0] != 1) {
        debug_print("Unsupported number of sides: %hhu", buffer[0]);
        retval = DSK_UNSUPPORTED_FORMAT;
        goto dsk2dirend;
    }

    // Read track offsets
    BYTE track_offsets[0x100 - 0x34];
    if (is_extended && n_tracks >= ARRAY_LENGTH(track_offsets)) {
        retval = DSK_TOO_LARGE;
        goto dsk2dirend;
    }
    if (is_extended)
        f_lseek(&fd, 0x34);
    read_and_validate(fd, track_offsets, br, is_extended ? n_tracks : 2);

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
    for (i = 0; i < 64; ++i) {
        FSIZE_T sector_adr;
        if ((retval = get_sector_adr(&fd, buffer, track_offsets, is_extended, is_system, dir_track, i / 16, &sector_adr)) != DSK_OK) goto dsk2dirend;

        // Skip to directory entry (past gap length, filler byte and sector info)
        f_lseek(&fd, sector_adr + ((i % 16) * 32));

        // Check user
        read_and_validate(fd, buffer, br, 1);
        if (buffer[0] != 0) continue;

        // Read filename and attributes
        WORD attr;
        BYTE filename[13];
        if ((retval = read_filename(&fd, filename, &attr)) != DSK_OK) goto dsk2dirend;
        if (filename[0] == '\0') continue;
        debug_print("Filename: %s", filename);
        debug_print("Attributes: %hu", attr);

        // Read extent
        read_and_validate(fd, buffer, br, 3);
        WORD extent = ((WORD)buffer[2] << 5) | (buffer[0] & 0x1F);
        debug_print("Extent: %u", extent);

        // Read record count
        BYTE record_count;
        read_and_validate(fd, &record_count, br, 1);
        debug_print("Record count: %hhu", record_count);

        // Read block list
        BYTE block_list[16];
        read_and_validate(fd, block_list, br, 16);

        FIL out_fd;
        if (f_open(&out_fd, (TCHAR*)filename, (extent ? FA_OPEN_APPEND : FA_CREATE_ALWAYS) | FA_WRITE) != FR_OK)
            return DSK_FILE_ERROR;

        // Write out file data
        for (unsigned j = 0; j < 16 && block_list[j] && record_count && retval == DSK_OK; ++j) {
            BYTE data[512];
            BYTE data_track = (block_list[j] * 2) / 9;
            BYTE data_sector = (block_list[j] * 2) % 9;

            debug_print("Data block %hhx starts at track %hhu, sector %hhu", block_list[j], data_track, data_sector);

            // Read and write block to disk (and respect record count)
            for (unsigned k = 0; k < 2 && record_count; ++k) {
                // Get sector address
                if ((retval = get_sector_adr(&fd, buffer, track_offsets, is_extended, is_system, data_track, data_sector, &sector_adr)) != DSK_OK) break;
                debug_print("Data at %.5lx", sector_adr);

                // Read sector
                f_lseek(&fd, sector_adr);
                if (f_read(&fd, data, 512, &br) != FR_OK || br < 512) {
                    retval = DSK_FILE_ERROR;
                    break;
                }

                // Write sector to file
                UINT btw = MIN(512, record_count * 128);
                record_count = (record_count > 4) ? record_count - 4 : 0;
                if (f_write(&out_fd, data, btw, &br) != FR_OK || br < btw) {
                    retval = DSK_FILE_ERROR;
                    break;
                }

                increment_sector(&data_track, &data_sector);
            }
        }

        f_close(&out_fd);
        if (retval != DSK_OK)
            goto dsk2dirend;
    }

dsk2dirend:
    return (f_close(&fd) == FR_OK) ? retval : -1;

#undef read_and_validate
}

int dir2dsk(const TCHAR* path)
{
    return -1;
}
