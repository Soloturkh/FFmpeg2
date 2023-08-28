/*
 * SMOOTH SMOOTH STREAMING MPEG segmenter
 * Copyright (c) 2023 Defans System
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFORMAT_SMOOTH_H
#define AVFORMAT_SMOOTH_H
#include "avformat.h"

// See ISO/IEC 23009-1:2014 5.3.9.4.4
typedef enum {
    SMOOTH_TMPL_ID_UNDEFINED = -1,
    SMOOTH_TMPL_ID_ESCAPE,
    SMOOTH_TMPL_ID_REP_ID,
    SMOOTH_TMPL_ID_NUMBER,
    SMOOTH_TMPL_ID_BANDWIDTH,
    SMOOTH_TMPL_ID_TIME,
} SMOOTHTmplId;


void ff_smooth_fill_tmpl_params(char *dst, size_t buffer_size, const char *template, int rep_id, int number, int bit_rate, int64_t time);

#endif /* AVFORMAT_SMOOTH_H */
