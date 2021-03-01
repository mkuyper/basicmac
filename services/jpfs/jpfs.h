// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _jpfs_h_
#define _jpfs_h_

#ifdef JPFS_IMPL
#include JPFS_IMPL
#endif

enum {
    JPFS_MAX_SIZE = 504,
};

void jpfs_init (void* log1, void* log2, int size);

bool jpfs_save (const uint8_t* ufid, const void* data, uint32_t sz);
bool jpfs_read (const uint8_t* ufid, void* data, uint32_t* psz);
bool jpfs_remove (const uint8_t* ufid);

#endif
