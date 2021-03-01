// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

// Journaled PicoFS
// A simple, flat, log-structured file system.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ufl.h"
#include "jpfs.h"

// Stats / Limits
//
// Block size:          32 bytes (8 words)
// Max # blocks/file    16
// Max file size:       504 bytes (16*32-8 [2 words: info and crc], fits in 2^9-1)
// Max # of files:      64 (2^6)
// Data in 1st block:   28 bytes (7 words)
// Data in cont blocks: 32 bytes (8 words)

typedef union {
    uint32_t info;
    uint32_t w[8];
} jpfs_block;

static const uint32_t JPFS_MAGIC = 0x5346504A;

enum {
    JPFS_ID_NEXT_OFF = 128,
};

static struct {
    jpfs_block* log[2];
    int free;
    int nblocks;
    int idx;
} state;

// I=erased, O=written
//
// Info word:
//  3          2          1          0
// 10987654 32109876 54321098 76543210
//                            iiiiiiII - entry block
// bbbbbbbb bbbbbbbs ssssssss iiiiiiOI - data start block
// dddddddd dddddddd dddddddd dddddddO - data cont. block
//
// i = file id
// s = file size
// b = extra bits
// d = file data
//
// The least-significant bit of the first (info) word of a data continuation
// block is always written (non-erased state). For every block, this missing
// bit is stored in the bits field in the info word of the data start block.
//
// The CRC over the data start block and any data continuation blocks is stored
// in the last word of the last data block.
//
// Note: Tainting the info field of an entry of data start block turns it into
// a data continuation block which are harmless and basically ignored when
// traversing the log.
//
// Entry block:
// w[0]    - info
// w[1..3] - ufid
// w[4..6] - reserved
// w[7]    - crc
//
// Data start/continuation block:
// w[0]    - info
// w[1..7] - file data

enum {
    JPFS_ENTRY_M = 0x3,
    JPFS_DATAS_M = 0x3,
    JPFS_DATAC_M = 0x1,
#if ufl_bitdefault == 1
    JPFS_ENTRY = 3,
    JPFS_DATAS = 1,
    JPFS_DATAC = 0,
#else
    JPFS_ENTRY = 0,
    JPFS_DATAS = 2,
    JPFS_DATAC = 1,
#endif
};

static inline bool is_entry (uint32_t info) {
    return (info & JPFS_ENTRY_M) == JPFS_ENTRY;
}

static inline bool is_data_start (uint32_t info) {
    return (info & JPFS_DATAS_M) == JPFS_DATAS;
}

static inline bool is_data_cont (uint32_t info) {
    return (info & JPFS_DATAC_M) == JPFS_DATAC;
}

static inline uint32_t info_id (uint32_t info) {
    return (info >> 2) & 0x3f;
}

static inline uint32_t info_sz (uint32_t info) {
    return (info >> 8) & 0x1ff;
}

static inline uint32_t info_bits (uint32_t info) {
    return (info >> 17);
}

static inline uint32_t info_entry (uint32_t id) {
    return (id << 2) | JPFS_ENTRY;
}

static inline uint32_t info_data_start (uint32_t id, uint32_t size, uint32_t bits) {
    return (bits << 17) | (size << 8) | (id << 2) | JPFS_DATAS;
}

#ifndef JPFS_ASSERT
#define JPFS_ASSERT(expr) do { } while( 0 )
#endif

#ifndef JPFS_LOG
#define JPFS_LOG(...) do { } while( 0 )
#endif

#ifndef JPFS_LOG_NEWLINE
#define JPFS_LOG_NEWLINE "\n"
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define JPFS_LE 1
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define JPFS_LE 0
#else
#error "Unknown byte order"
#endif

#ifndef jpfs_crc32
static uint32_t jpfs_crc32 (uint32_t crc, const void* ptr, uint32_t nwords) {
    const uint8_t* buf = ptr;
    uint32_t len = nwords << 2;
    uint32_t byte, mask;
    crc = ~crc;
    while( len-- != 0 ) {
	byte = *buf++;
	crc = crc ^ byte;
	for( int i = 7; i >= 0; i-- ) {
	    mask = -(crc & 1);
	    crc = (crc >> 1) ^ (0xedb88320 & mask);
	}
    }
    return ~crc;
}
#endif

static void load_block_ex (jpfs_block* blk, int i, int logidx) {
    ufl_read(blk, state.log[logidx] + i, 8);
}

static void load_block (jpfs_block* blk, int i) {
    load_block_ex(blk, i, state.idx);
}

static void append_block_ex (jpfs_block* blk, int* pfree, int logidx) {
    JPFS_ASSERT(*pfree < state.nblocks); // GCOVR_EXCL_LINE
    ufl_write(state.log[logidx] + (*pfree)++, blk, 8, false);
}

static void append_block (jpfs_block* blk) {
    append_block_ex(blk, &state.free, state.idx);
}

static void taint_block (int i) {
    ufl_wr_u4(&state.log[state.idx][i].info, ufl_bitdefault ? 0 : ~0);
}

static void taint_log (int idx) {
    ufl_wr_u4(&state.log[idx][0].info, ufl_bitdefault ? 0 : ~0);
}

static bool untainted_block (int i) {
    jpfs_block blk;
    load_block(&blk, i);
    for( int w = 0; w < 8; w++ ) {
        if (blk.w[w] != (ufl_bitdefault ? ~0 : 0) ) {
            return false;
        }
    }
    return true;
}

static int find_free (void) {
    int free = state.nblocks;
    int i = free;
    while( i-- > 1 && untainted_block(i) ) {
        free = i;
    }
    return free;
}

static bool load_next_entry (jpfs_block* blk, int* start) {
    for( int i = *start; i < state.free; i++ ) {
        load_block(blk, i);
        if( is_entry(blk->info) && jpfs_crc32(0, blk->w, 7) == blk->w[7] ) {
            *start = i + 1;
            return true;
        }
    }
    return false;
 }

static int find_entry (const uint8_t* ufid, int* start) {
    uint64_t mask = 0;
    jpfs_block blk;
    int id;
    while( load_next_entry(&blk, start) ) {
        id = info_id(blk.info);
        if( memcmp(blk.w + 1, ufid, 12) == 0 ) {
            return id;
        }
        mask |= (1ULL << id);
    }
    if( ~mask ) {
        id = __builtin_ctzll(~mask); // 0-63
    } else {
        id = 64; // invalid -- no more file ids
    }
    return id - JPFS_ID_NEXT_OFF; // next available id, ensure negative
}

static int find_next_data (uint32_t id, int* start) {
    int sz;
    uint32_t crc;
    int first = -1;
    for( int i = *start; i < state.free; i++ ) {
        jpfs_block blk;
        load_block(&blk, i);
        if( first < 0 ) {
            // searching for start block
            if( is_data_start(blk.info) && info_id(blk.info) == id ) {
                sz = info_sz(blk.info);
                if( sz > JPFS_MAX_SIZE ) {
                    JPFS_LOG("jpfs: invalid size in block (%d)" JPFS_LOG_NEWLINE, sz);
                    goto invalid;
                }
                first = i;
                crc = 0;
                if( sz <= (28 - 4) ) {
                    goto last;
                } else {
                    sz -= 28; // Note: size might go negative if block is not fully used, but that's ok
                    goto more;
                }
            }
        } else {
            // expecting continuation blocks
            if( !is_data_cont(blk.info) ) {
                JPFS_LOG("jpfs: unexpected block" JPFS_LOG_NEWLINE);
                goto invalid;
            }
            if( sz <= (32 - 4) ) {
last:
                if( jpfs_crc32(crc, blk.w, 7) != blk.w[7] ) {
                    JPFS_LOG("jpfs: invalid CRC" JPFS_LOG_NEWLINE);
invalid:
                    first = -1;
                    continue;
                }
                *start = i + 1;
                return first;
            } else {
                sz -= 32; // Note: size might go negative if block is not fully used, but that's ok
more:
                crc = jpfs_crc32(crc, blk.w, 8);
            }
        }
    }
    return -1;
}

static void ncopy (uint8_t** pdst, uint32_t* pn, void* src, uint32_t n) {
    if( *pn < n ) {
        n = *pn;
    }
    memcpy(*pdst, src, n);
    *pn -= n;
    *pdst += n;
}

static void fixup_size (uint32_t* pssz, uint32_t* pdsz) {
    uint32_t dsz = *pdsz;
    *pdsz = *pssz;
    if( dsz < *pssz ) {
        *pssz = dsz;
    }
}

// NOTE: this function assumes all is well
static void read_data (int i, uint8_t* dst, uint32_t* pdsz) {
    jpfs_block blk;
    load_block(&blk, i++);

    uint32_t bits = info_bits(blk.info);
    uint32_t sz = info_sz(blk.info);

#if ufl_bitdefault == 0
    bits = ~bits;
#endif

    fixup_size(&sz, pdsz);

    ncopy(&dst, &sz, blk.w + 1, 28);

    while( sz > 0 ) {
        load_block(&blk, i++);
#if ufl_bitdefault == 1
        blk.w[0] |= (bits & 1);
#else
        blk.w[0] ^= (bits & 1);
#endif
        ncopy(&dst, &sz, blk.w, 32);
        bits >>= 1;
    }
}

static void ocopy (void* dst, uint32_t n, const uint8_t** psrc, uint32_t* pn) {
    if( *pn < n ) {
        memset((uint8_t*) dst + *pn, 0, n - *pn);
        n = *pn;
    }
    memcpy(dst, *psrc, n);
    *pn -= n;
    *psrc += n;
}

// NOTE: this function assumes all is well
static void write_data (const uint8_t* src, uint32_t n, uint32_t id) {
    // collect bits
    uint32_t bits = 0;
    for( int i = 28 + (JPFS_LE ? 0 : 3), j = 0; i < n; i += 32, j++ ) {
        bits |= (src[i] & 1) << j;
    }
    jpfs_block blk;
    blk.info = info_data_start(id, n, bits);
    uint32_t crc = 0;
    bool last = (n <= 24);
    ocopy(blk.w + 1, 28, &src, &n);
    while( !last ) {
        crc = jpfs_crc32(crc, blk.w, 8);
        append_block(&blk);
        last = (n <= 28);
        ocopy(blk.w, 32, &src, &n);
#if ufl_bitdefault == 1
        blk.w[0] &= ~1;
#else
        blk.w[0] |= 1;
#endif
    }
    blk.w[7] = jpfs_crc32(crc, blk.w, 7);
    append_block(&blk);
}

static int find_data (int id, int start, bool prune) {
    int dsid = -1;
    int nid;
    while( (nid = find_next_data(id, &start)) >= 0 ) {
        if( prune && dsid >= 0) {
            taint_block(dsid);
        }
        dsid = nid;
    }
    return dsid;
}

static uint32_t calc_nblocks (uint32_t sz) {
   return (sz <= 24) ? 1 : 2 + ((sz - 25) >> 5);   // (sz - 25) <=> ((sz - (28 + 28)) + 31)
}

static void log_erase (int idx) {
    ufl_erase(state.log[idx], state.nblocks << 3);
}

static void log_activate (int idx, int free) {
    state.idx = idx;
    if( free < 0 ) {
        state.free = find_free();
    } else {
        state.free = free;
    }
    if( ufl_rd_u4(&state.log[idx]->info) != JPFS_MAGIC ) {
        ufl_wr_u4(&state.log[idx]->info, JPFS_MAGIC);
    }
    if( ufl_rd_u4(&state.log[!idx]->info) == JPFS_MAGIC ) {
        taint_log(!idx);
    }
}

static void log_rotate (void) {
    uint32_t nidx = !state.idx;

    // erase new log
    log_erase(nidx);

    int free = 1; // free index (new log)

    jpfs_block blk;
    int start = 1;
    while( load_next_entry(&blk, &start) ) {
        int dsid = find_data(info_id(blk.info), start, false);
        if( dsid < 0 ) {
            JPFS_LOG("jpfs: skipping orphaned entry" JPFS_LOG_NEWLINE);
        } else {
            // copy entry
            append_block_ex(&blk, &free, nidx);
            // copy first datablock
            load_block(&blk, dsid++);
            uint32_t nblocks = calc_nblocks(info_sz(blk.info));
            append_block_ex(&blk, &free, nidx);
            // copy additional datablocks
            while( --nblocks > 0 ) {
                load_block(&blk, dsid++);
                append_block_ex(&blk, &free, nidx);
            }
        }
    }
    log_activate(nidx, free);
    JPFS_LOG("jpfs: log rotated" JPFS_LOG_NEWLINE);
}

bool jpfs_save (const uint8_t* ufid, const void* data, uint32_t sz) {
    if( sz > JPFS_MAX_SIZE ) {
        JPFS_LOG("jpfs: invalid size %u" JPFS_LOG_NEWLINE, sz);
        return false;
    }

    uint32_t nblocks = calc_nblocks(sz);

    int start = 1;
    int id = find_entry(ufid, &start);

    if( id < 0 ) {
        nblocks += 1; // extra block for entry
    }

    for( bool rotated = false; nblocks > (state.nblocks - state.free); ) {
        JPFS_LOG("jpfs: journal full (%d/%d)" JPFS_LOG_NEWLINE,
                nblocks, (state.nblocks - state.free));
        if( rotated ) {
            JPFS_LOG("jpfs: giving up" JPFS_LOG_NEWLINE);
            return false;
        }
        log_rotate();
        rotated = true;
    }

    if( id < 0 ) {
        id += JPFS_ID_NEXT_OFF;
        if( id >= 64 ) {
            JPFS_LOG("jpfs: no more file ids available" JPFS_LOG_NEWLINE);
            return false;
        }
        jpfs_block blk;
        blk.info = info_entry(id);
        memcpy(blk.w + 1, ufid, 12);
        memset(blk.w + 4, 0, 12);
        blk.w[7] = jpfs_crc32(0, blk.w, 7);
        append_block(&blk);
    }

    write_data(data, sz, id);

    return true;
}

bool jpfs_read (const uint8_t* ufid, void* data, uint32_t* psz) {
    int start = 1;
    int id = find_entry(ufid, &start);

    if( id < 0 ) {
        return false;
    }

    int dsid = find_data(id, start, true);

    if( dsid < 0 ) {
        JPFS_LOG("jpfs: ignoring orphaned entry" JPFS_LOG_NEWLINE);
        return false;
    }

    read_data(dsid, data, psz);

    return true;
}

bool jpfs_remove (const uint8_t* ufid) {
    int start = 1;
    int id = find_entry(ufid, &start);

    if( id < 0 ) {
        return false;
    }

    taint_block(start-1); // entry block is 1 before start
    return true;
}

void jpfs_init (void* log1, void* log2, int size) {
    state.nblocks = size >> 5;
    state.log[0] = log1;
    state.log[1] = log2;

    if( ufl_rd_u4(&state.log[0]->info) == JPFS_MAGIC ) {
        log_activate(0, -1);
    } else if( ufl_rd_u4(&state.log[1]->info) == JPFS_MAGIC ) {
        log_activate(1, -1);
    } else {
        // create new log
        log_erase(0);
        log_activate(0, 1);
    }
}



#ifdef JPFS_TEST
//------------------------------------------------------------------------------
// JPFS Test Suite

// GCOVR_EXCL_START

#include <criterion/criterion.h>

// 177e46bcef453f70-76755bbc
static const uint8_t UFID_TEST1[12] = {
    0x70, 0x3f, 0x45, 0xef, 0xbc, 0x46, 0x7e, 0x17, 0xbc, 0x5b, 0x75, 0x76
};

static const char* LOREM_IPSUM = "Lorem ipsum dolor sit amet, consectetur "
"adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna "
"aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi "
"ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in "
"voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
"occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim "
"id est laborum.";

static const char* LOREM_IPSUM2 = "Vitae turpis massa sed elementum tempus "
"egestas. Turpis massa sed elementum tempus egestas sed sed. Ultrices vitae "
"auctor eu augue ut lectus arcu. In hendrerit gravida rutrum quisque non tellus. "
"Ultrices sagittis orci a scelerisque purus semper eget duis. Vel eros donec ac "
"odio tempor orci dapibus. Feugiat nibh sed pulvinar proin gravida hendrerit "
"lectus a. Enim neque volutpat ac tincidunt vitae.";

Test(jpfs, init) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);

    jpfs_init(j1, j2, 4096);
    cr_assert_eq(state.idx, 0);
    cr_assert_eq(state.log[0], j1);
    cr_assert_eq(state.log[1], j2);
    cr_assert_eq(state.free, 1);
    cr_assert_eq(state.nblocks, 128);

    jpfs_init(j1, j2, 4096);
    cr_assert_eq(state.idx, 0);
    cr_assert_eq(state.log[0], j1);
    cr_assert_eq(state.log[1], j2);
    cr_assert_eq(state.free, 1);
    cr_assert_eq(state.nblocks, 128);

    jpfs_init(j2, j1, 4096);
    cr_assert_eq(state.idx, 1);
    cr_assert_eq(state.log[0], j2);
    cr_assert_eq(state.log[1], j1);
    cr_assert_eq(state.free, 1);
    cr_assert_eq(state.nblocks, 128);
}

Test(jpfs, save_read) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    bool rv;
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(rv, true);

    uint8_t buf[1024];
    uint32_t sz;

    // read with 0 / NULL
    memset(buf, 0, sizeof(buf));
    sz = 0;
    rv = jpfs_read(UFID_TEST1, NULL, &sz);
    cr_assert_eq(rv, true);
    cr_assert_eq(sz, strlen(LOREM_IPSUM));

    // read with short
    memset(buf, 0, sizeof(buf));
    sz = 50;
    rv = jpfs_read(UFID_TEST1, buf, &sz);
    cr_assert_eq(rv, true);
    cr_assert_eq(sz, strlen(LOREM_IPSUM));
    cr_assert_arr_eq(buf, LOREM_IPSUM, 50);
    cr_assert_eq(buf[50], 0);

    // read with same
    memset(buf, 0, sizeof(buf));
    sz = strlen(LOREM_IPSUM);
    rv = jpfs_read(UFID_TEST1, buf, &sz);
    cr_assert_eq(rv, true);
    cr_assert_eq(sz, strlen(LOREM_IPSUM));
    cr_assert_arr_eq(buf, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(buf[strlen(LOREM_IPSUM)], 0);

    // read with more
    memset(buf, 0, sizeof(buf));
    sz = sizeof(buf);
    rv = jpfs_read(UFID_TEST1, buf, &sz);
    cr_assert_eq(rv, true);
    cr_assert_eq(sz, strlen(LOREM_IPSUM));
    cr_assert_arr_eq(buf, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(buf[strlen(LOREM_IPSUM)], 0);
}

static void rndfill (uint8_t* buf, int n) {
    while( n-- > 0 ) {
        *buf++ = rand();
    }
}

Test(jpfs, load_test) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    const int nfiles = 30;
    const int msize = 75;

    struct {
        uint8_t ufid[12];
        uint8_t buf[512];
        int n;
        bool deleted;
    } files[nfiles];

    uint8_t buf[1024];
    uint32_t sz;

    // randomize the test, but deterministically
    srand(JPFS_MAGIC);

    bool rv;

    for( int i = 0; i < nfiles; i++ ) {
        rndfill(files[i].ufid, 12);
        files[i].n = rand() % msize;
        rndfill(files[i].buf, files[i].n);
        rv = jpfs_save(files[i].ufid, files[i].buf, files[i].n);
        cr_assert_eq(rv, true);
        files[i].deleted = false;
    }

    for( int i = 0; i < nfiles; i++ ) {
        memset(buf, 0, sizeof(buf));
        sz = sizeof(buf);
        rv = jpfs_read(files[i].ufid, buf, &sz);
        cr_assert_eq(rv, true);
        cr_assert_eq(sz, files[i].n);
        cr_assert_arr_eq(buf, files[i].buf, sz);
    }

    for( int x = 0; x < 5000; x++ ) {
        int f = rand() % nfiles;

        if( (rand() % 100) < 10 ) {
            // delete file
            rv = jpfs_remove(files[f].ufid);
            cr_assert_eq(rv, !files[f].deleted);
            files[f].deleted = true;
        } else {
            // create/change file
            files[f].n = rand() % msize;
            rndfill(files[f].buf, files[f].n);
            rv = jpfs_save(files[f].ufid, files[f].buf, files[f].n);
            cr_assert_eq(rv, true);
            files[f].deleted = false;
        }

        for( int i = 0; i < nfiles; i++ ) {
            memset(buf, 0, sizeof(buf));
            sz = sizeof(buf);
            rv = jpfs_read(files[i].ufid, buf, &sz);
            if( files[i].deleted ) {
                cr_assert_eq(rv, false);
            } else {
                cr_assert_eq(rv, true);
                cr_assert_eq(sz, files[i].n);
                cr_assert_arr_eq(buf, files[i].buf, sz);
            }
        }

        if( (rand() % 100) < 5 ) {
            jpfs_init(j1, j2, 4096); // remount
        }
    }
}

static jpfs_block* block_direct (int i) {
    return flashsimul_direct(state.log[state.idx] + i);
}

static void verify_lorem (const uint8_t* ufid, const char* lorem) {
    uint8_t buf[1024];
    uint32_t sz = sizeof(buf);
    memset(buf, 0, sz);
    bool rv = jpfs_read(UFID_TEST1, buf, &sz);
    cr_assert_eq(rv, true);
    cr_assert_eq(sz, strlen(lorem));
    cr_assert_arr_eq(buf, lorem, sz);
    cr_assert_eq(buf[sz], 0);
}

Test(jpfs, truncate_log_crc) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    bool rv;

    // Write LI
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(rv, true);

    // Write LI2
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM2, strlen(LOREM_IPSUM2));
    cr_assert_eq(rv, true);

    // tamper with last block (CRC)
    jpfs_block* blk = block_direct(state.free - 1);
    blk->w[1] ^= 1; // bit flip!

    // verify original
    verify_lorem(UFID_TEST1, LOREM_IPSUM);
}

Test(jpfs, truncate_log_type) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    bool rv;

    // write li
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(rv, true);

    // write li2
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM2, strlen(LOREM_IPSUM2));
    cr_assert_eq(rv, true);

    // tamper with last block (crc)
    jpfs_block* blk = block_direct(state.free - 1);
    blk->w[0] = info_entry(0);

    // verify original
    verify_lorem(UFID_TEST1, LOREM_IPSUM);
}

Test(jpfs, tamper_log_size) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    bool rv;

    // write li
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(rv, true);

    // write li2
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM2, strlen(LOREM_IPSUM2));
    cr_assert_eq(rv, true);

    // tamper with data block (size)
    int di = find_data(0, 1, false);
    cr_assert_geq(di, 0);
    jpfs_block* blk = block_direct(di);
    blk->w[0] = info_data_start(0, 505, 0);

    // verify original
    verify_lorem(UFID_TEST1, LOREM_IPSUM);
}

Test(jpfs, orphan_read) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    bool rv;

    // write li
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(rv, true);

    // taint data block
    int di = find_data(0, 1, false);
    cr_assert_geq(di, 0);
    taint_block(di);

    // verify orphan
    uint32_t sz = 0;
    rv = jpfs_read(UFID_TEST1, NULL, &sz);
    cr_assert_eq(rv, false);
}

Test(jpfs, orphan_rotate) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    bool rv;

    // write li
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(rv, true);

    // taint data block
    int di = find_data(0, 1, false);
    cr_assert_geq(di, 0);
    taint_block(di);

    // rotate log
    log_rotate();

    // verify orphan
    uint32_t sz = 0;
    rv = jpfs_read(UFID_TEST1, NULL, &sz);
    cr_assert_eq(rv, false);
}

Test(jpfs, save_too_large) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    uint8_t buf[1024];
    bool rv;

    rv = jpfs_save(UFID_TEST1, buf, 504);
    cr_assert_eq(rv, true);
    
    rv = jpfs_save(UFID_TEST1, buf, 505);
    cr_assert_eq(rv, false);
}

Test(jpfs, save_no_space) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    uint8_t ufid[12] = { 0 };
    uint8_t buf[1024];
    bool rv;

    for( int i = 0; i <= 7; i ++ ) {
        ufid[11] = i;
        rv = jpfs_save(ufid, buf, 500);
        cr_assert_eq(rv, (i == 7) ? false : true);
    }
}

Test(jpfs, save_too_many) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 8192);
    jpfs_init(j1, j2, 8192);

    uint8_t ufid[12] = { 0 };
    uint8_t buf[1024];
    bool rv;

    for( int i = 0; i <= 64; i ++ ) {
        ufid[11] = i;
        rv = jpfs_save(ufid, buf, 10);
        cr_assert_eq(rv, (i == 64) ? false : true);
    }
}

Test(jpfs, tamper_entry_crc) {
    void* f = flashsimul_init();
    void* j1 = (void*) ((uintptr_t) f);
    void* j2 = (void*) ((uintptr_t) f + 4096);
    jpfs_init(j1, j2, 4096);

    bool rv;

    // write li
    rv = jpfs_save(UFID_TEST1, LOREM_IPSUM, strlen(LOREM_IPSUM));
    cr_assert_eq(rv, true);

    // tamper with crc of entry block
    jpfs_block* blk = block_direct(1);
    blk->w[1] ^= 1; // bit flip!

    // verify gone
    uint32_t sz = 0;
    rv = jpfs_read(UFID_TEST1, NULL, &sz);
    cr_assert_eq(rv, false);
}

// GCOVR_EXCL_STOP

#endif
