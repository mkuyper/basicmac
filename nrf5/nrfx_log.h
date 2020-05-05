// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _nrfx_log_h
#define _nrfx_log_h


#if 0
extern void debug_printf (char const *format, ...);
#define NRFX_LOG_ERROR(format, ...)     debug_printf(format, ##__VA_ARGS__)
#define NRFX_LOG_WARNING(format, ...)   debug_printf(format, ##__VA_ARGS__)
#define NRFX_LOG_INFO(format, ...)      debug_printf(format, ##__VA_ARGS__)
#define NRFX_LOG_DEBUG(format, ...)     debug_printf(format, ##__VA_ARGS__)
#else
#define NRFX_LOG_ERROR(format, ...)
#define NRFX_LOG_WARNING(format, ...)
#define NRFX_LOG_INFO(format, ...)
#define NRFX_LOG_DEBUG(format, ...)
#endif

#define NRFX_LOG_HEXDUMP_ERROR(p_memory, length)
#define NRFX_LOG_HEXDUMP_WARNING(p_memory, length)
#define NRFX_LOG_HEXDUMP_INFO(p_memory, length)
#define NRFX_LOG_HEXDUMP_DEBUG(p_memory, length)

#define NRFX_LOG_ERROR_STRING_GET(error_code) "-"

#endif
