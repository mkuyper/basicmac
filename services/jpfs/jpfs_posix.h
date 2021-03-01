// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _jpfs_posix_h_
#define _jpfs_postx_h_

#include <assert.h>
#include <stdio.h>

#define JPFS_ASSERT(expr)  assert(expr)
#define JPFS_LOG(...)      printf(__VA_ARGS__)

#endif
