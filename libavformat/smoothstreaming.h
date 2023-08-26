/*
 * Microsoft Smooth Streaming (mss) demuxer
 * Copyright (c) 2013 Florent Tribouilloy
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

/**
 * @file
 * header for smoothstreaming demuxer
 */

#ifndef AVFORMAT_SMOOTHSTREAMING_H
#define AVFORMAT_SMOOTHSTREAMING_H
#include "isom.h"

typedef struct Fragment
{
    uint64_t duration;
    int index;
    uint64_t start_ts;
} Fragment;

typedef struct QualityVideo
{
    int max_width;
    int max_height;
    int width;
    int height;
} QualityVideo;

typedef struct QualityAudio
{
    int sample_rate;
    int nb_channels;
    int bit_per_sample;
    int packet_size;
    int audio_tag;
    int wave_format_ex;
} QualityAudio;

/* QualityLevel */
typedef struct Quality
{
    int is_video;
    int is_audio;

    int index; /* stream index */
    uint32_t fourcc;
    uint64_t bit_rate; /* bit rate of this fragments stream */
    char *private_str; /* Codec private data */

    int stream_id;

    AVFormatContext *ctx; /* context of the video/audio stream */
    AVFormatContext *parent; /* needed when reading data from fragment */
    URLContext *input; /* current fragment */
    AVInputFormat *fmt; /* input format, fragment format */
    AVPacket pkt; /* packet to send to the demuxer */

    uint8_t *read_buffer; /* buffer needed by read_data */
    AVIOContext pb;
    QualityVideo *qv;
    QualityAudio *qa;
} Quality;

/* StreamIndex */
typedef struct StreamIndex {
    int is_video;
    int is_audio;
    int is_text;

    int index;
    char url[MAX_URL_SIZE];
    int64_t last_load_time;

    int max_width;
    int max_height;

    int display_width;
    int display_height;

    int nb_qualities;
    int cur_quality;
    Quality *qualities;

    int nb_fragments;
    int cur_frag; /* fragment to add */
    Fragment *frags;

    AVFormatContext *parent;
    AVFormatContext *ctx;
    AVInputFormat *fmt;
    AVIOContext pb;
    AVPacket pkt;
    uint8_t *read_buffer;
    URLContext *input;
} StreamIndex;

/* SmoothStreamingMedia */
typedef struct MSSContext {
    char url[MAX_URL_SIZE];

    int is_live;
    uint64_t duration;

    int major;
    int minor;
    int xml_error;

    int nb_stream_index;
    StreamIndex *stream_index;

    int64_t first_timestamp;
    int64_t seek_timestamp;
    int seek_flags;

    int video_id;
    int audio_id;

    AVIOInterruptCB *interrupt_callback;
} MSSContext;

#define SMOOTH_BUFF_SIZE 4096
#define INITIAL_BUFFER_SIZE 32768
#define NB_DIGIT_UINT64 20

int smoothstreaming_parse_manifest(AVFormatContext *s, const char *url, AVIOContext *in);

#endif /* AVFORMAT_SMOOTHSTREAMING_H */
