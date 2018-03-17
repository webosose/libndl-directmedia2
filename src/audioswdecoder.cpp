/*
 * Copyright (c) 2008-2018 LG Electronics, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0



 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.

 * SPDX-License-Identifier: Apache-2.0
 */

#include "audioswdecoder.h"

using namespace NDL_Esplayer;

#define LOGTAG "audioswdecoder"
#include "debug.h"

AudioSwDecoder::AudioSwDecoder()
{
    NDLLOG(LOGTAG, NDL_LOGI, "%s", __func__);
    avctx_ = NULL;
    avframe_ = NULL;
    swrctx_ = NULL;

    output_channels_ = 0;
    output_buffer_size_ = 0;
    output_buffer_data_ = NULL;
    input_sample_fmt_ = AV_SAMPLE_FMT_NONE;
    output_sample_fmt_ = AV_SAMPLE_FMT_NONE;
}

AudioSwDecoder::~AudioSwDecoder()
{
    NDLLOG(LOGTAG, NDL_LOGI, "%s", __func__);

    if (output_buffer_data_)
        av_free(output_buffer_data_);
    output_buffer_data_ = NULL;
    output_buffer_size_ = 0;
    output_channels_ = 0;

    if (swrctx_)
        swr_free(&swrctx_);
    swrctx_ = NULL;

    if (avframe_)
        av_frame_free(&avframe_);
    avframe_ = NULL;

    if (avctx_)
        avcodec_free_context(&avctx_);
    avctx_ = NULL;
}

bool AudioSwDecoder::OpenAudio(enum AVCodecID codec_id)
{
    AVCodec* codec = NULL;
    avcodec_register_all();

    codec = avcodec_find_decoder(codec_id);

    if (!codec)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "%s: codec find error AVCodecID:0x%x", __func__, (int)codec_id);
        return false;
    }

    avctx_ = avcodec_alloc_context3(codec);
    NDLLOG(LOGTAG, NDL_LOGI, "sample_fmt:%d, AV_SAMPLE_FMT_S16:%d", avctx_->sample_fmt, AV_SAMPLE_FMT_S16);
    avctx_->debug_mv = 0;
    avctx_->debug = 0;
    avctx_->workaround_bugs = 1;

    avctx_->channels              = audio_stream_info_.channels;
    avctx_->sample_rate           = audio_stream_info_.sample_rate;
    avctx_->block_align           = audio_stream_info_.block_align;
    avctx_->bit_rate              = audio_stream_info_.bit_rate;
    avctx_->bits_per_coded_sample = audio_stream_info_.bits_per_coded_sample;

    if (avctx_->bits_per_coded_sample == 0)
        avctx_->bits_per_coded_sample = 16;

    if (avcodec_open2(avctx_, codec, NULL) < 0)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "%s: codec open error AVCodecID:0x%", __func__, (int)codec_id);
        return false;
    }

    avframe_ = av_frame_alloc();
    input_sample_fmt_ = AV_SAMPLE_FMT_NONE;

    //  output_sample_fmt_ = avctx_->sample_fmt == AV_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;
    output_sample_fmt_ = AV_SAMPLE_FMT_S16;
    NDLLOG(LOGTAG, NDL_LOGD, "%s: output sample format set to AV_SAMPLE_FMT_S16", __func__);

    return true;
}

int AudioSwDecoder::DecodeAudio(unsigned char* data, int size, double dts, double pts)
{
    int ret = 0;
    int got_frame = 0;
    int outline_size = 0;
    int output_size = 0;
    AVPacket avpkt;

    av_init_packet(&avpkt);
    avpkt.data = data;
    avpkt.size = size;

    ret = avcodec_decode_audio4( avctx_, avframe_, &got_frame, &avpkt);

    output_size = av_samples_get_buffer_size(&outline_size, avctx_->channels, avframe_->nb_samples, output_sample_fmt_, 1);

    if (output_buffer_size_ < output_size)
    {
        NDLLOG(LOGTAG, NDL_LOGD, "%s: buffer allocate %d to %d", __func__, output_buffer_size_, output_size);
        output_buffer_data_ = (unsigned char*)av_realloc(output_buffer_data_, output_size + FF_INPUT_BUFFER_PADDING_SIZE);
        output_buffer_size_ = output_size;
    }

    if (avctx_->sample_fmt != output_sample_fmt_)
    {
        //NDLLOG(LOGTAG, NDL_LOGD, "need to convert format. in sample fmt:%d, output sample fmt:%d", avctx_->sample_fmt, output_sample_fmt_);

        if (swrctx_ && (avctx_->sample_fmt != input_sample_fmt_ || output_channels_ != avctx_->channels))
        {
            swr_free(&swrctx_);
            output_channels_ = avctx_->channels;
        }

        if (!swrctx_)
        {
            input_sample_fmt_ = avctx_->sample_fmt;
            swrctx_ = swr_alloc_set_opts(NULL,
                    av_get_default_channel_layout(avctx_->channels),
                    output_sample_fmt_, avctx_->sample_rate,
                    av_get_default_channel_layout(avctx_->channels),
                    avctx_->sample_fmt, avctx_->sample_rate,
                    0, NULL);

            if (!swrctx_ || swr_init(swrctx_) < 0)
            {
                NDLLOG(LOGTAG, NDL_LOGE, "%s: format convert initialization error %d to %d", __func__, avctx_->sample_fmt, output_sample_fmt_);
                return 0;
            }
        }

        uint8_t *out_planes[avctx_->channels];
        if (av_samples_fill_arrays(out_planes, NULL, output_buffer_data_, avctx_->channels, avframe_->nb_samples, output_sample_fmt_, 1) < 0 ||
                swr_convert(swrctx_, out_planes, avframe_->nb_samples, (const uint8_t **)avframe_->data, avframe_->nb_samples) < 0)
        {
            NDLLOG(LOGTAG, NDL_LOGE, "%s: format convert error %d to %d", __func__, avctx_->sample_fmt, output_sample_fmt_);
            output_size = 0;
        }
    }
    else
    {
        //NDLLOG(LOGTAG, NDL_LOGD, "copy to a contiguous buffer");
        uint8_t *out_planes[avctx_->channels];
        if (av_samples_fill_arrays(out_planes, NULL, output_buffer_data_, avctx_->channels, avframe_->nb_samples, output_sample_fmt_, 1) < 0 ||
                av_samples_copy(out_planes, avframe_->data, 0, 0, avframe_->nb_samples, avctx_->channels, output_sample_fmt_) < 0 )
        {
            output_size = 0;
        }
    }

    return ret;
}
