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

#include "libavutil/time.h"
#include "libavformat/expat.h"

#include "avformat.h"
#include "url.h"
#include "smoothstreaming.h"

#include <ctype.h>

static void print_context(AVFormatContext *s)
{
    uint64_t i,j;
    MSSContext *c = s->priv_data;

    av_log(s, AV_LOG_VERBOSE, "is live : %s\n", c->is_live ? "yes" : "no");
    av_log(s, AV_LOG_VERBOSE, "duration : %"PRIu64"\n", c->duration);
    av_log(s, AV_LOG_VERBOSE, "major : %d\n", c->major);
    av_log(s, AV_LOG_VERBOSE, "minor : %d\n", c->minor);
    av_log(s, AV_LOG_VERBOSE, "number of stream index : %d\n", c->nb_stream_index);


    for (i = 0; i < c->nb_stream_index ; ++i) {
        StreamIndex *si = &c->stream_index[i];
        const char *type = si->is_video ? "video" : si->is_audio ? "audio" : "unknown";

        av_log(s, AV_LOG_VERBOSE, "\tStream %s index : %d\n",
               type, si->index);
        av_log(s, AV_LOG_VERBOSE, "\tUrl : %s\n", si->url);

        if (si->is_video) {
            av_log(s, AV_LOG_VERBOSE, "\tMaximum widthXheight : %dX%d\n",
                   si->max_width, si->max_height);

            av_log(s, AV_LOG_VERBOSE, "\tDisplay widthXheight : %dX%d\n",
                   si->display_width, si->display_height);
        }

        av_log(s, AV_LOG_VERBOSE, "\t%d qualities for this stream\n", si->nb_qualities);

        for (j = 0; j < si->nb_qualities ; ++j) {
            Quality *q = &si->qualities[j];

            av_log(s, AV_LOG_VERBOSE, "\t\tIndex of this %s quality: %d\n", type, q->index);
            av_log(s, AV_LOG_VERBOSE, "\t\tbit_rate : %"PRIu64"\n", q->bit_rate);
            av_log(s, AV_LOG_VERBOSE, "\t\tfourcc : %.4s\n", (char*)&q->fourcc);
            av_log(s, AV_LOG_VERBOSE, "\t\tprivate data : %s\n", q->private_str);
            if (q->is_video) {
                av_log(s, AV_LOG_VERBOSE, "\t\tvideo widthXheight : %dX%d\n",
                       q->qv->width, q->qv->height);
                av_log(s, AV_LOG_VERBOSE, "\t\tvideo maxwidthXmaxheight : %dX%d\n",
                       q->qv->max_width, q->qv->max_height);
            } else if (q->is_audio) {
                av_log(s, AV_LOG_VERBOSE, "\t\tsample_rate : %d\n", q->qa->sample_rate);
                av_log(s, AV_LOG_VERBOSE, "\t\tnb of channels : %d\n", q->qa->nb_channels);
                av_log(s, AV_LOG_VERBOSE, "\t\tbit per sample : %d\n", q->qa->bit_per_sample);
                av_log(s, AV_LOG_VERBOSE, "\t\tpacket size : %d\n", q->qa->packet_size);
                av_log(s, AV_LOG_VERBOSE, "\t\ttag audio : %d\n", q->qa->audio_tag);
            }
        }

        av_log(s, AV_LOG_VERBOSE, "\t%d fragments for this stream\n", si->nb_fragments);
        for (j=0; j < si->nb_fragments ; ++j)
        {
            av_log(s, AV_LOG_VERBOSE, "\t\tfragment duration : %"PRIu64"\n", si->frags[j].duration);
            av_log(s, AV_LOG_VERBOSE, "\t\tfragment index : %d\n", si->frags[j].index);
            av_log(s, AV_LOG_VERBOSE, "\t\tfragment start timestamp : %"PRIu64"\n", si->frags[j].start_ts);
        }
    }

}

static int parse_media(AVFormatContext *s, const char **attribute)
{
    MSSContext *c = s->priv_data;
    char *tmp = NULL;
    int i = 0;

    c->is_live = 0;
    c->nb_stream_index = 0;
    c->stream_index = NULL;
    c->duration = -1;
    c->major = -1;
    c->minor = -1;

    for (i = 0; attribute[i] && attribute[i + 1]; i += 2) {
        if (strcmp(attribute[i], "isLive") == 0
            && strcmp(attribute[i + 1], "true") == 0)
            c->is_live = 1;
        else if (strcasecmp(attribute[i], "Duration") == 0) {
            c->duration = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                c->duration = 0;
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "MajorVersion")) {
            c->major = strtol(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                c->major = -1;
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "MinorVersion")) {
            c->minor = strtol(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                c->minor = -1;
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "TimeScale")) {
            /* TODO */
        } else if (!strcasecmp(attribute[i], "LookAheadCount")) {
            /* TODO */
        } else if (!strcasecmp(attribute[i], "DVRWindowLength")) {
            /* TODO */
        } else {
            av_log(s, AV_LOG_ERROR, "Manifest : SmoothStreamingMedia: field %s"
                   " is not recognized\n", attribute[i]);
            return AVERROR_INVALIDDATA;
        }
    }
    if (c->duration == -1 || c->major == -1 || c->minor == -1) {
        av_log(s, AV_LOG_ERROR, "Manifest : SmoothStreamingMedia needs all its mandatory fields\n");
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int make_stream_url(AVFormatContext *s, StreamIndex* si, const char *url)
{
    MSSContext *c = s->priv_data;
    char *find_pos;
    uint64_t diff;
    int len;

    find_pos = strcasestr(c->url, "/manifest");
    if (!find_pos)
        diff = strlen(c->url);
    else
        diff = find_pos - c->url;
    len = FFMIN(sizeof(si->url), diff + 1);
    snprintf(si->url, len, "%s", c->url);

    len -= 1;

    snprintf(si->url + len, sizeof(si->url) - len, "/%s", url);

    return 0;
}

static int parse_index(AVFormatContext *s, const char **attribute)
{
    MSSContext *c = s->priv_data;
    int stream_i = c->nb_stream_index;
    int i = 0;
    char *tmp = NULL;
    const char *url = NULL;
    StreamIndex *si = NULL;

    ++c->nb_stream_index;
    c->stream_index = av_realloc(c->stream_index, c->nb_stream_index * sizeof(*c->stream_index));
    if (!c->stream_index) {
        goto not_enought_memory;
    }
    si = &c->stream_index[stream_i];

    si->display_width = -1;
    si->display_height = -1;
    si->max_width = -1;
    si->max_height = -1;
    si->is_video = 0;
    si->is_audio = 0;
    si->index = -1;
    si->nb_qualities = -1;
    si->fmt = NULL;
    si->pb.av_class = NULL;
    si->input = NULL;
    si->pkt.data = NULL;

    for (i = 0; attribute[i] && attribute[i + 1]; i += 2) {
        if (!strcasecmp(attribute[i], "Type")) {
            if (strcasecmp(attribute[i + 1], "video") == 0)
                si->is_video = 1;
            else if (strcasecmp(attribute[i + 1], "audio") == 0)
                si->is_audio = 1;
            else if (strcasecmp(attribute[i + 1], "text") == 0)
                si->is_text = 1; /* TODO: subtitles */
            else
                return AVERROR_INVALIDDATA;
        } else if (!strcasecmp(attribute[i], "QualityLevels")) {
            /* Some server use this value for fun and make a segmentation fault */
            /* si->nb_qualities = strtoll(attribute[i + 1], &tmp, 10); */
            /* if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') */
            /*     { */
            /*         return AVERROR_INVALIDDATA; */
            /*     } */
        } else if (!strcasecmp(attribute[i], "Chunks")) {
            si->nb_fragments = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "Index")) {
            si->index = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "MaxWidth")) {
            si->max_width = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "MaxHeight")) {
            si->max_height = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "DisplayWidth")) {
            si->display_width = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "DisplayHeight")) {
            si->display_height = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0') {
                return AVERROR_INVALIDDATA;
            }
        } else if (!strcasecmp(attribute[i], "Url")) {
            url = attribute[i + 1];
        } else if (!strcasecmp(attribute[i], "Subtype")) {
            av_log(s, AV_LOG_INFO, "Subtype : %s\n", attribute[i + 1]);
        } else if (!strcasecmp(attribute[i], "SubtypeEventControl")) {
            av_log(s, AV_LOG_INFO, "SubtypeEventControl : %s\n", attribute[i + 1]);
        } else if (!strcasecmp(attribute[i], "ParentStream")) {
            av_log(s, AV_LOG_INFO, "ParentStream : %s\n", attribute[i + 1]);
        } else if (!strcasecmp(attribute[i], "Name")) {
            av_log(s, AV_LOG_INFO, "name : %s\n", attribute[i + 1]);
        } else {
            av_log(s, AV_LOG_WARNING, "Manifest : StreamIndex : option %s is not recognized\n", attribute[i]);
        }
    }

    if (si->index == -1) {
        si->index = 0;
    }

    make_stream_url(s, si, url);

    if (si->nb_qualities != -1) {
        si->qualities = av_mallocz(si->nb_qualities * sizeof(*si->qualities));
        if (!si->qualities) {
            goto not_enought_memory;
        }
    } else {
        si->qualities = NULL;
        si->nb_qualities = 0;
    }
    si->cur_quality = -1;

    si->frags = av_mallocz(si->nb_fragments * sizeof(*si->frags));
    if (!si->frags) {
        goto not_enought_memory;
    }
    si->cur_frag = -1;
    si->last_load_time = av_gettime();

    return 0;

 not_enought_memory:
    return AVERROR(ENOMEM);
}

static int parse_quality(AVFormatContext *s, const char **attribute)
{
    MSSContext *c = s->priv_data;
    int stream_i = c->nb_stream_index - 1;
    StreamIndex *si = NULL;
    Quality *q = NULL;

    int i = 0;
    char *tmp = NULL;
    int64_t bit_rate = -1;
    int index = -1;
    int max_width = -1, max_height = -1;
    int width = -1, height = -1;
    uint64_t audio_tag = 0, packet_size = 0, channels = 0;
    uint64_t sample_rate = 0, b_p_sample = 0;
    const char *fourcc = NULL, *private_data = NULL;
    int wave_format_ex = 0;

    if (stream_i == -1)
        return AVERROR_INVALIDDATA;

    si = &c->stream_index[stream_i];
    ++si->cur_quality;

    if (si->cur_quality == si->nb_qualities) {
        ++si->nb_qualities;
        si->qualities = av_realloc(si->qualities, si->nb_qualities * sizeof(*si->qualities));
        if (!si->qualities)
            return AVERROR(ENOMEM);
    }
    q = &si->qualities[si->cur_quality];

    for (i = 0; attribute[i] && attribute[i + 1]; i += 2)
    {
        if (strcasecmp(attribute[i], "Index") == 0)
        {
            index = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "Bitrate") == 0)
        {
            bit_rate = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "MaxWidth") == 0)
        {
            max_width = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "MaxHeight") == 0)
        {
            max_height = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "Width") == 0)
        {
            width = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "Height") == 0)
        {
            height = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "AudioTag") == 0)
        {
            audio_tag = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "BitsPerSample") == 0)
        {
            b_p_sample = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "SamplingRate") == 0)
        {
            sample_rate = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "PacketSize") == 0)
        {
            packet_size = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "Channels") == 0)
        {
            channels = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "FourCC") == 0)
        {
            fourcc = attribute[i + 1];
        }
        else if (strcasecmp(attribute[i], "CodecPrivateData") == 0)
        {
            private_data = attribute[i + 1];
        }
        else if (strcasecmp(attribute[i], "WaveFormatEx") == 0)
        {
            fourcc = "WMAP";
            private_data = attribute[i + 1];
            wave_format_ex = 1;
        }
        else
            av_log(NULL, AV_LOG_WARNING, "Unreconized %s='%s'\n", attribute[i], attribute[i + 1]);
    }

    /* No field Index in the QualityLevel, only one is supported */
    if (index == -1)
        index = 0;

    if (bit_rate == -1)
        return AVERROR_INVALIDDATA;

    q->bit_rate = bit_rate;
    q->index = index;
    if (!fourcc || strlen(fourcc) != 4)
        return AVERROR_INVALIDDATA;
    q->fourcc = MKTAG(tolower(fourcc[0]), tolower(fourcc[1]), tolower(fourcc[2]), tolower(fourcc[3]));

    if (private_data) {
        q->private_str = strdup(private_data);
        if (!q->private_str)
            return AVERROR(ENOMEM);
    } else
        q->private_str = NULL;
    q->is_video = si->is_video;
    q->is_audio = si->is_audio;
    if (q->is_audio) {
        QualityAudio *qa = av_mallocz(sizeof(*qa));
        if (!qa)
            return AVERROR(ENOMEM);

        qa->nb_channels = channels;
        qa->sample_rate = sample_rate;
        qa->bit_per_sample = b_p_sample;
        qa->audio_tag = audio_tag;
        qa->packet_size = packet_size;
        q->qa = qa;
        qa->wave_format_ex = wave_format_ex;
    } else if (q->is_video) {
        QualityVideo *qv = av_mallocz(sizeof(*qv));
        if (!qv)
            return AVERROR(ENOMEM);

        qv->max_width = max_width;
        qv->max_height = max_height;
        qv->width = width;
        qv->height = height;
        q->qv = qv;
    }
    return 0;
}

static int parse_frags(AVFormatContext *s, const char **attribute)
{
    MSSContext *c = s->priv_data;
    int stream_i = c->nb_stream_index - 1;
    StreamIndex *si = NULL;
    Fragment *f = NULL;
    int i = 0, j;
    char *tmp = NULL;
    int64_t start_ts = -1;

    if (stream_i == -1)
        return AVERROR_INVALIDDATA;

    si = &c->stream_index[stream_i];
    ++si->cur_frag;
    f = &si->frags[si->cur_frag];

    for (i = 0; attribute[i] && attribute[i + 1]; i += 2)
    {
        if (strcmp(attribute[i], "n") == 0)
        {
            f->index = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
            /* start_ts = 0; */
            /* for (j = 0; j < f->index - 1; ++j) */
            /*     start_ts += si->frags[j].duration; */
        }
        else if (strcasecmp(attribute[i], "d") == 0)
        {
            f->duration = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else if (strcasecmp(attribute[i], "t") == 0)
        {
            f->start_ts = strtoll(attribute[i + 1], &tmp, 10);
            if (!tmp || tmp == attribute[i + 1] || *tmp != '\0')
            {
                return AVERROR_INVALIDDATA;
            }
        }
        else
        {
            av_log(NULL, AV_LOG_WARNING, "Unrecognized %s='%s'\n", attribute[i], attribute[i + 1]);
            return AVERROR_INVALIDDATA;
        }
    }

    if (f->index == 0) {
        f->index = si->cur_frag;
    }
    if (start_ts == -1 && f->start_ts == 0) {
        start_ts = 0;
        for (j = 0; j < f->index - 1; ++j)
            start_ts += si->frags[j].duration;
        f->start_ts = start_ts;
    }

    return 0;
}

/* first when start element is encountered */
static void start_element(void *data, const char *element, const char **attribute)
{
    AVFormatContext *s = data;
    MSSContext *c = s->priv_data;
    int error = 0;

    if (strcasecmp(element, "SmoothStreamingMedia") == 0)
    {
        if ((error = parse_media(s, attribute)) < 0)
            goto fail;
    }

    else if (strcasecmp(element, "StreamIndex") == 0)
    {
        if ((error = parse_index(s, attribute)) < 0)
            goto fail;
    }

    else if (strcasecmp(element, "QualityLevel") == 0)
    {
        if ((error = parse_quality(s, attribute)) < 0)
            goto fail;
    }

    else if (strcasecmp(element, "c") == 0)
    {
        if ((error = parse_frags(s, attribute)) < 0)
            goto fail;
    }

    else
    {
        av_log(s, AV_LOG_WARNING, "Unrecognized element %s", element);
    }

    return ;

 fail:
    c->xml_error = error;
}

static void end_element(void *data, const char *element)
{
}

static void handle_data(void *data, const char *content, int length)
{
}

int smoothstreaming_parse_manifest(AVFormatContext *s, const char *url, AVIOContext *in)
{
    MSSContext *c = s->priv_data;
    int ret = 0;
    char *line = NULL;
    int pos = 0;
    int len = 0;
    XML_Parser      parser;

    while (!avio_feof(in))
    //while (!url_feof(in))
    {
        line = av_realloc(line, pos + SMOOTH_BUFF_SIZE);
        if (!line)
        {
            return AVERROR(ENOMEM);
        }
        len = avio_read(in, line + pos, SMOOTH_BUFF_SIZE - 1);
        if (len >= 0)
        {
            pos += len;
            line[pos] = '\0';
        }
        else
        {
            break;
        }
    }

    parser = XML_ParserCreate(NULL);

    if (!parser)
    {
        av_log(s, AV_LOG_ERROR, "Unable to allocate memory for the libexpat XML parser\n");
        return AVERROR(ENOMEM);
    }

    c->xml_error = 0;
    XML_SetUserData(parser, s);
    XML_SetElementHandler(parser, start_element, end_element);
    XML_SetCharacterDataHandler(parser, handle_data);

    if (XML_Parse(parser, line, pos, XML_TRUE) == XML_STATUS_ERROR)
    {
        av_log(s, AV_LOG_ERROR, "Error: %s\n", XML_ErrorString(XML_GetErrorCode(parser)));
    }

    XML_ParserFree(parser);
    av_free(line);

    if (c->xml_error >= 0)
        print_context(s);

    return c->xml_error;
}
