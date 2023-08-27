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

/*
 * Used the hls demuxer as example. (libavformat/hls.c)
 * And piff specification
 * (http://www.iis.net/learn/media/smooth-streaming/protected-interoperable-file-format)
 * BUILD
 */
#include "libavutil/dict.h"

#include "libavutil/avstring.h"
#include "libavutil/time.h"
#include "libavcodec/avcodec.h"

#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "url.h"
#include "isom.h"
#include "avc.h"
#include "smoothstreaming.h"
#include "riff.h"

#include <string.h>

static int make_frag_url(StreamIndex* si, uint64_t bit_rate, uint64_t start_ts,
                         char *frag_url, int frag_url_size)
{
    uint64_t diff, pos, wr = 0;
    char *find_pos;
    int ret = 0;
    int len = 0;

    find_pos = av_stristr(si->url, "{bitrate}");
    if (!find_pos)
        return AVERROR_INVALIDDATA;
    diff = find_pos - si->url;
    len = FFMIN(frag_url_size, diff + 1);
    snprintf(frag_url, len, "%s", si->url);
    pos = len - 1;
    wr = pos;

    wr += snprintf(frag_url + wr, frag_url_size - wr, "%"PRIu64"", bit_rate);
    pos += 9;

    find_pos = av_stristr(si->url + pos, "{start time}");
    if (!find_pos)
        return AVERROR_INVALIDDATA;
    diff = find_pos - (si->url + pos);
    len = FFMIN(frag_url_size - wr, diff + 1);
    snprintf(frag_url + wr, len, "%s", si->url + pos);
    pos += len - 1;
    wr += len - 1;

    wr += snprintf(frag_url + wr, frag_url_size - wr, "%"PRIu64"", start_ts);
    pos += 12;
    wr += snprintf(frag_url + wr, frag_url_size - wr, "%s", si->url + pos);
    return ret;
}


static int read_data(void *opaque, uint8_t *buf, int buf_size)
{
    StreamIndex *si = opaque;
    MSSContext *c = si->parent->priv_data;
    Fragment *frag = NULL;
    AVDictionary *opts = NULL;
    char url[MAX_URL_SIZE];
    int ret = 0;

 restart:
    if (!si->input) {
        int64_t reload_interval = 0;
        ++si->cur_frag;
        if (!c->is_live && si->cur_frag >= si->nb_fragments)
            return AVERROR_EOF;

        reload_interval = si->nb_fragments > 0 && c->is_live ?
            si->frags[si->cur_frag].duration :
            c->duration;
    reload:
        if (c->is_live &&
            av_gettime() - si->last_load_time >= reload_interval) {
            if ((ret = smoothstreaming_parse_manifest(si->parent, c->url, si->parent->pb)) < 0)
                return ret;
            reload_interval = c->duration * 500000LL;
        }
        if (si->cur_frag >= si->nb_fragments) {
            if (si->cur_frag == si->nb_fragments)
                return AVERROR_EOF;
            while (av_gettime() - si->last_load_time < reload_interval)
                {
                    if (ff_check_interrupt(c->interrupt_callback))
                        return AVERROR_EXIT;
                    av_usleep(100*1000);
                }
            /* Enough time has elapsed since the last reload */
            goto reload;
        }

        if (si->cur_frag < si->nb_fragments) {
            av_dict_set(&opts, "seekable", "0", 0);
            frag = &si->frags[si->cur_frag];
            make_frag_url(si, si->qualities[si->cur_quality].bit_rate, frag->start_ts, &url[0], sizeof(url));
            //ret = ffurl_open(&si->input, url, AVIO_FLAG_READ,
            //               &si->parent->interrupt_callback, &opts);
	    ret = avio_open2(&si->input, url, AVIO_FLAG_READ, 
                             &si->parent->interrupt_callback, &opts);
            av_dict_free(&opts);
            if (ret < 0)
                return ret;
        }
        else
            return AVERROR_EXIT;
    }

    ret = ffurl_read(si->input, buf, buf_size);
    if (ret > 0) {
        return ret;
    }

    ffurl_close(si->input);
    si->input = NULL;
    if (ret < 0)
        return ret;
    goto restart;
}

static int smoothstreaming_set_extradata(AVCodecParameters *codecpar, const char *extra)
{
    int size = 0;
    uint8_t *buf = NULL;
    int new_size;

    new_size = strlen(extra) / 2;
    if (new_size >= INT_MAX)
        return AVERROR_INVALIDDATA;
    buf = av_mallocz((new_size + AV_INPUT_BUFFER_PADDING_SIZE) * sizeof(*buf));
    if (!buf)
        return AVERROR(ENOMEM);
    size = ff_hex_to_data(buf, extra);
    codecpar->extradata_size = size;
    codecpar->extradata = buf;
    return codecpar->extradata_size;
}

static int smoothstreaming_set_extradata_h264(AVCodecParameters *codecpar, const char *extra)
{
    int size = 0, ret = 0;
    int i, count;
    uint8_t *buf = NULL;
    int new_size;
    AVIOContext *bio = NULL;

    new_size = strlen(extra) / 2;
    if (new_size >= INT_MAX)
        return AVERROR_INVALIDDATA;
    buf = av_mallocz((new_size + AV_INPUT_BUFFER_PADDING_SIZE) * sizeof(*buf));
    if (!buf)
        return AVERROR(ENOMEM);
    size = ff_hex_to_data(buf, extra);
    codecpar->extradata_size = size;
    codecpar->extradata = buf;

    for (i = 0, count=0; i + 3 < size; ++i) {
        if (buf[i] == 0
            && buf[i + 1] == 0
            && buf[i + 2] == 0
            && buf[i + 3] == 1) {
            ++count;
            i += 3;
        }
    }

    new_size = size + count * 4;
    buf = av_mallocz((new_size + AV_INPUT_BUFFER_PADDING_SIZE) * sizeof(*buf));
    if (!buf)
        return AVERROR(ENOMEM);

    bio = avio_alloc_context(buf, new_size, 0, NULL, NULL, NULL, NULL);
    if (!bio)
        return AVERROR(ENOMEM);
    if ((ret = ff_isom_write_avcc(bio, codecpar->extradata, codecpar->extradata_size)) < 0)
        return ret;
    codecpar->extradata_size = bio->buf_ptr - bio->buffer;
    codecpar->extradata = bio->buffer;

    return codecpar->extradata_size;
}

static int open_audio_demuxer(StreamIndex *si, AVStream *st)
{
    Quality *q = &si->qualities[si->cur_quality];
    QualityAudio *qa = q->qa;
    AVStream *ist = NULL;
    int ret = 0;

    if (qa->wave_format_ex != 0) {
        int  len = 0;
        uint8_t *buf = NULL;
        AVIOContext *bio = NULL;

        len = strlen(q->private_str) / 2;
        if (len >= INT_MAX)
            return AVERROR_INVALIDDATA;
        buf = av_mallocz((len + AV_INPUT_BUFFER_PADDING_SIZE) * sizeof(*buf));
        if (!buf)
            return AVERROR(ENOMEM);
        len = ff_hex_to_data(buf, q->private_str);
        bio = avio_alloc_context(buf, len, 0, NULL, NULL, NULL, NULL);
        if (!bio)
            return AVERROR(ENOMEM);
	//ret = ff_get_wav_header(bio, st->codec, len);
        ret = ff_get_wav_header(st, bio, st->codecpar, len, 0);
        if (ret < 0)
            return ret;
        st->need_parsing = AVSTREAM_PARSE_FULL_RAW;
        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
        av_free(buf);

    } else {
        ist = si->ctx->streams[0]; /* only one stream by fragment */
        avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);
        avcodec_copy_context(st->codecpar, ist->codec);
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = ff_codec_get_id(ff_codec_movaudio_tags, q->fourcc);
        if (q->fourcc == MKTAG('a', 'a', 'c', 'l'))
            st->codecpar->codec_id = AV_CODEC_ID_AAC;
        else if (q->fourcc == MKTAG('w', 'm', 'a', 'p'))
            st->codecpar->codec_id = AV_CODEC_ID_WMAPRO;

        st->codecpar->sample_rate = qa->sample_rate;
        st->codecpar->bits_per_coded_sample = qa->bit_per_sample;
        st->codecpar->channels = qa->nb_channels;
        if (qa->bit_per_sample == 16)
            st->codecpar->sample_fmt = AV_SAMPLE_FMT_S16;
        st->time_base.den = qa->sample_rate;
        st->time_base.num = 1;
        st->codecpar->time_base.den = st->time_base.den;
        st->codecpar->time_base.num = st->time_base.num;
        st->codecpar->block_align = qa->packet_size;

        if ((ret = smoothstreaming_set_extradata(st->codecpar, q->private_str)) < 0)
            return ret;
        st->codecpar->bit_rate = q->bit_rate;
    }
    si->parent->bit_rate += q->bit_rate;

    return 0;
}

static int open_video_demuxer(StreamIndex *si, AVStream *st)
{
    Quality *q = &si->qualities[si->cur_quality];
    QualityVideo *qv = q->qv;
    AVStream *ist = NULL;
    int ret = 0;

    ist = si->ctx->streams[0]; /* only one stream by fragment */
    avcodec_copy_context(st->codecpar, ist->codec);
    /* FIXME : the pts is not correct, video going to fast */
    avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    if (q->fourcc == MKTAG('h', '2', '6', '4')
        || q->fourcc == MKTAG('a', 'v', 'c', '1')) {
        st->codecpar->codec_id = AV_CODEC_ID_H264;
        st->codecpar->pix_fmt = AV_PIX_FMT_YUV420P;
        if ((ret = smoothstreaming_set_extradata_h264(st->codecpar, q->private_str)) < 0)
            return ret;
    }
    else if (q->fourcc == MKTAG('w', 'v', 'c', '1')) {
        st->codecpar->codec_id = AV_CODEC_ID_VC1;
        if ((ret = smoothstreaming_set_extradata(st->codecpar, q->private_str)) < 0)
            return ret;
    }

    st->codecpar->bit_rate = q->bit_rate;
    st->codecpar->width = qv->width != -1 ? qv->width : qv->max_width;
    st->codecpar->height = qv->height != -1 ? qv->height : qv->max_height;
    st->codecpar->flags &= ~CODEC_FLAG_GLOBAL_HEADER;

    return 0;
}

static int open_demux_codec(StreamIndex *si, AVStream *st)
{
    Quality *q = &si->qualities[si->cur_quality];
    int ret = 0;

    if (!q || !st)
        return AVERROR_INVALIDDATA;

    st->codecpar->codec_tag = q->fourcc;
    if (si->is_video != 0) {
        ret = open_video_demuxer(si, st);
    } else if (si->is_audio != 0) {
        ret = open_audio_demuxer(si, st);
    }
    return ret;
}

static int open_demuxer_io(StreamIndex *si)
{
    AVDictionary *opts = NULL;
    char url[MAX_URL_SIZE];
    int ret = 0;

    si->read_buffer = av_malloc(INITIAL_BUFFER_SIZE);
    if (!si->read_buffer)
        return AVERROR(ENOMEM);

    ffio_init_context(&si->pb, si->read_buffer, INITIAL_BUFFER_SIZE, 0, si,
                      read_data, NULL, NULL);
    si->pb.seekable = 0;

    make_frag_url(si, si->qualities[si->cur_quality].bit_rate,
                  si->frags[si->cur_frag].start_ts, &url[0], sizeof(url));

    si->ctx->pb = &si->pb;
    ret = av_probe_input_buffer(&si->pb, &si->fmt, url, si->parent, 0, 0);
    if (ret < 0) {
        av_log(si->parent, AV_LOG_ERROR, "Error when loading first fragment"
               " '%s'\n", url);
        avformat_free_context(si->ctx);
        si->ctx = NULL;
        return ret;
    }

    av_dict_set(&opts, "movdflags", "smooth", 0);
    av_dict_set(&opts, "seekable", "0", 0);
    ret = avformat_open_input(&si->ctx, url, si->fmt, &opts);
    if (ret < 0) {
        av_log(si->parent, AV_LOG_ERROR, "Error when opening the first fragment"
               " '%s'\n", url);
        avformat_free_context(si->ctx);
        si->ctx = NULL;
        return ret;
    }

    return ret;
}

static int open_demuxer(AVFormatContext *s, StreamIndex *si)
{
    AVStream *st = NULL;
    int ret = 0;

    if (!si || si->nb_fragments == 0)
        return AVERROR_INVALIDDATA;

    /* initilize the format context */
    if (!(si->ctx = avformat_alloc_context())) {
        return AVERROR(ENOMEM);
    }
    si->ctx->interrupt_callback = s->interrupt_callback;

    /* Create new AVStreams the stream of this fragments */
    st = avformat_new_stream(s, NULL);
    if (!st) {
        return AVERROR(ENOMEM);
    }
    si->qualities[si->cur_quality].stream_id = st->index;

    si->parent = s;
    if ((ret = open_demuxer_io(si)) < 0)
        return ret;

    si->ctx->ctx_flags &= ~CODEC_FLAG_GLOBAL_HEADER;
    if ((ret = avformat_find_stream_info(si->ctx, NULL)) < 0)
        return ret;

    if ((ret = open_demux_codec(si, st)) < 0)
        return ret;

    return ret;
}

/* make a function to initilize video_id and audio_id */
static int get_init_streams_id(MSSContext *c)
{
    unsigned int i = 0, j = 0;

    c->video_id = -1;
    c->audio_id = -1;
    for (i=0; i < c->nb_stream_index; ++i) {
        StreamIndex *si = &c->stream_index[i];

        if (si && si->is_video != 0) {
            c->video_id = i;
            for (j = 0; j < si->nb_qualities; ++j) {
                Quality *q = &si->qualities[j];
                if (c->video_id == -1) {
                    si->cur_quality = j;
                } else if (si->display_width == q->qv->width && si->display_height == q->qv->height) {
                    si->cur_quality = j;
                    break;
                } else if (si->display_width == q->qv->max_width && si->display_height == q->qv->max_height) {
                    si->cur_quality = j;
                    break;
                }
            }
            si->cur_frag = 0;
        } else if (si && si->is_audio != 0) {
            c->audio_id = i;
            for (j = 0; j < si->nb_qualities; ++j) {
                if (si->cur_quality == -1) {
                    si->cur_quality = j;
                    break;
                }
            }
            si->cur_frag = 0;
        }
    }
    return 0;
}

static int smoothstreaming_read_header(AVFormatContext *s)
{
    MSSContext *c = s->priv_data;
    int ret = 0;

    /* Manifest is already here; copy the url to reach */
    snprintf(c->url, sizeof(c->url), "%s", s->filename);

    c->interrupt_callback = &s->interrupt_callback;

    if ((ret = smoothstreaming_parse_manifest(s, c->url, s->pb)) < 0)
        goto fail;

    if (c->nb_stream_index == 0 || c->duration == -1) {
        av_log(s, AV_LOG_ERROR, "No streams in the Manifest\n");
        ret = AVERROR_EOF;
        goto fail;
    }

    if (c->major != 2 || c->minor != 0)
        av_log(s, AV_LOG_WARNING, "Manifest : MajorVersion should be 2, MinorVersion should be 0\n");

    get_init_streams_id(c);

    av_log(s, AV_LOG_INFO, "Stream index for video : %d, audio : %d\n",
           c->video_id, c->audio_id);

    /* Open demuxer for video stream */
    if (c->video_id != -1 && (ret = open_demuxer(s, &c->stream_index[c->video_id])) < 0)
        goto fail;

    /* Open demuxer for audio stream */
    if (c->audio_id != -1 && (ret = open_demuxer(s, &c->stream_index[c->audio_id])) < 0)
        goto fail;

    c->first_timestamp = AV_NOPTS_VALUE;
    c->seek_timestamp  = AV_NOPTS_VALUE;

    av_log(s, AV_LOG_INFO, "Stream index for video : %d, audio : %d\n",
           c->video_id, c->audio_id);


    /* register the total duration for non live streams */
    if (!c->is_live)
        s->duration = c->duration / 10;

    return 0;

fail:
    return ret;
}

static int get_minstream_dts(MSSContext *c, int id, int minstream)
{
    StreamIndex *last_is = NULL;
    StreamIndex *si = &c->stream_index[id];
    int64_t this_dts = 0;
    int64_t last_dts = 0;
    AVStream *st = NULL;
    AVStream *last_st = NULL;

    /* Check if this stream has the packet with the lowest dts */
    if (si->pkt.data) {
        if (minstream < 0) {
            return id;
        } else {
            this_dts = si->pkt.dts;
            if (id == c->audio_id)
                last_is = &c->stream_index[c->video_id];
            else
                last_is = &c->stream_index[c->audio_id];
            last_dts = last_is->pkt.dts;
            st = si->ctx->streams[si->pkt.stream_index];
            last_st = last_is->ctx->streams[last_is->pkt.stream_index];

            if(st->start_time != AV_NOPTS_VALUE)
                this_dts -= st->start_time;
            if(last_st->start_time != AV_NOPTS_VALUE)
                last_dts -= last_st->start_time;

            if (av_compare_ts(this_dts, st->time_base, last_dts, last_st->time_base) < 0)
                minstream = id;
        }
    }
    return minstream;
}

static int treat_packet(MSSContext *c, AVPacket *pkt, StreamIndex *si, int *minvariant)
{
    int ret = 0;

    if (!si->pkt.data) {
        while (1) {
            int64_t ts_diff;
            AVStream *st = NULL;

            ret = av_read_frame(si->ctx, &si->pkt);

            if (ret < 0) {
                if (!url_feof(&si->pb) && ret != AVERROR_EOF)
                    return ret;
                av_init_packet(&si->pkt);
                si->pkt.data = NULL;
                break;
            } else {
                if (c->first_timestamp == AV_NOPTS_VALUE)
                    c->first_timestamp = si->pkt.dts;
            }

            if (c->seek_timestamp == AV_NOPTS_VALUE)
                break;

            if (si->pkt.dts == AV_NOPTS_VALUE) {
                c->seek_timestamp = AV_NOPTS_VALUE;
                break;
            }

            st = si->ctx->streams[si->pkt.stream_index];
            ts_diff = av_rescale_rnd(si->pkt.dts, AV_TIME_BASE,
                                     st->time_base.den, AV_ROUND_DOWN) -
                c->seek_timestamp;
            if (ts_diff >= 0 && (c->seek_flags  & AVSEEK_FLAG_ANY ||
                                 si->pkt.flags & AV_PKT_FLAG_KEY)) {
                c->seek_timestamp = AV_NOPTS_VALUE;
                break;
            }
        }
    }
    return 0;
}

static int smoothstreaming_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MSSContext *c = s->priv_data;
    StreamIndex *si = NULL;
    int ret, minstream = -1;

    if (c->video_id != -1) {
        ret = treat_packet(c, pkt, &c->stream_index[c->video_id], &minstream);
        if (ret < 0)
            return ret;
    }
    if (c->audio_id != -1) {
        ret = treat_packet(c, pkt, &c->stream_index[c->audio_id], &minstream);
        if (ret < 0) {
            return ret;
        }
    }

    if (c->video_id != -1) {
        minstream = get_minstream_dts(c, c->video_id, minstream);
        si = &c->stream_index[c->video_id];
    }

    if (c->audio_id != -1) {
        if (minstream != get_minstream_dts(c, c->audio_id, minstream)) {
            si = &c->stream_index[c->audio_id];
            minstream = c->audio_id;
        }
    }
    /* If we have a packet, return it */
    if (si && minstream >= 0) {
        *pkt = si->pkt;
        pkt->stream_index = si->qualities[si->cur_quality].stream_id;
        av_init_packet(&si->pkt);
        si->pkt.data = NULL;
        return 0;
    }
    return AVERROR_EOF;
}

static int smoothstreaming_close(AVFormatContext *s)
{
    MSSContext *c = s->priv_data;
    uint64_t i, j;

    for (i=0; i < c->nb_stream_index; ++i)
    {
        for (j=0; j < c->stream_index[i].nb_qualities; ++j) {
            free(c->stream_index[i].qualities[j].private_str);
        }
        av_free(c->stream_index[i].qualities);
        av_free(c->stream_index[i].frags);
    }
    av_free(c->stream_index);
    return 0;
}

static int find_fragments_ts(AVFormatContext *s, StreamIndex *si, int stream_index, int flags, int64_t timestamp, int *ret)
{
    MSSContext *c = s->priv_data;
    int j;

    /* Reset reading */
    int64_t pos = c->first_timestamp == AV_NOPTS_VALUE ? 0 :
        av_rescale_rnd(c->first_timestamp, 10, stream_index >= 0 ?
                       s->streams[stream_index]->time_base.den :
                       AV_TIME_BASE, flags & AVSEEK_FLAG_BACKWARD ?
                       AV_ROUND_DOWN : AV_ROUND_UP);

    if (si->input) {
        ffurl_close(si->input);
        si->input = NULL;
    }
    av_free_packet(&si->pkt);
    av_init_packet(&si->pkt);
    si->pkt.data = NULL;
    si->pb.eof_reached = 0;
    /* Clear any buffered data */
    si->pb.buf_end = si->pb.buf_ptr = si->pb.buffer;
    /* Reset the pos, to let the mpegts demuxer know we've seeked. */
    si->pb.pos = 0;

    /* Locate the segment that contains the target timestamp */
    for (j = 0; j < si->nb_fragments; ++j) {
        if (timestamp >= pos &&
            timestamp < pos + si->frags[j].duration) {
            si->cur_frag = j;
            *ret = 0;
            break;
        }
        pos += si->frags[j].duration;
    }
    return *ret;
}

static int smoothstreaming_seek(AVFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    MSSContext *c = s->priv_data;
    StreamIndex *si = NULL;
    int i, ret = 0;

    if ((flags & AVSEEK_FLAG_BYTE) || c->is_live)
        return AVERROR(ENOSYS);

    c->seek_flags     = flags;
    c->seek_timestamp = stream_index < 0 ? timestamp :
                        av_rescale_rnd(timestamp, AV_TIME_BASE,
                                       s->streams[stream_index]->time_base.den,
                                       flags & AVSEEK_FLAG_BACKWARD ?
                                       AV_ROUND_DOWN : AV_ROUND_UP);
    timestamp = av_rescale_rnd(timestamp, 10, stream_index >= 0 ?
                               s->streams[stream_index]->time_base.den :
                               AV_TIME_BASE, flags & AVSEEK_FLAG_BACKWARD ?
                               AV_ROUND_DOWN : AV_ROUND_UP);

    ret = AVERROR(EIO);
    for (i = 0; i < c->nb_stream_index; ++i) {
        si = &c->stream_index[i];
        if (si->qualities[si->cur_quality].stream_id == stream_index) {
            find_fragments_ts(s, &c->stream_index[i], stream_index, flags, timestamp, &ret);
            if (ret)
                c->seek_timestamp = AV_NOPTS_VALUE;
        }
    }

    return ret;
}

static int smoothstreaming_read_probe(AVProbeData *pd)
{
    int ret = 0;

    if (pd->filename && !strcasecmp(pd->filename + strlen(pd->filename) - 9, "/manifest"))
        ret += AVPROBE_SCORE_MAX / 2;
    if (pd->buf && pd->buf_size > 19 && !strncasecmp(pd->buf, "<?xml version=\"1.0\"", 19))
        ret += AVPROBE_SCORE_MAX / 4;
    /* TODO: check for SmoothStreamingMedia */
    return ret;
}

AVInputFormat ff_smoothstreaming_demuxer = {
    .name           = "smoothstreaming,mss",
    .long_name      = NULL_IF_CONFIG_SMALL("Microsoft Smooth Streaming"),
    .priv_data_size = sizeof(MSSContext),
    .read_probe     = smoothstreaming_read_probe,
    .read_header    = smoothstreaming_read_header,
    .read_packet    = smoothstreaming_read_packet,
    .read_close     = smoothstreaming_close,
    .read_seek      = smoothstreaming_seek,
};
