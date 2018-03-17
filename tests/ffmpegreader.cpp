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

#include "ffmpegreader.h"

#include <assert.h>
#include <fcntl.h>
#include <algorithm>


#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

size_t av_to_omx_framerate(AVRational fr) {
    if (fr.den > 0 && fr.num > 0)
        return (long long) (1<<16) * fr.num / fr.den;
    return 25 * (1<<16);
}

size_t av_time_base_to_omx_framerate(AVRational fr) {
    return av_to_omx_framerate({fr.den, fr.num});
}

    FfmpegReader::FfmpegReader(bool video, bool audio)
    : enable_video_(video)
      , enable_audio_(audio)
{
}

FfmpegReader::~FfmpegReader()
{
    if(avfctx_)
        av_close_input_file(avfctx_);
}

bool FfmpegReader::init(const char* input_file)
{
    av_register_all();

    int result = avformat_open_input(&avfctx_, input_file, NULL, NULL);
    if (result < 0) {
        fprintf(stderr, "avformat_open_input: error %d\n", result);
        return false;
    }

    result = av_find_stream_info(avfctx_);
    if (result < 0) {
        fprintf(stderr, "av_find_stream_info: error %d\n", result);
        return false;
    }
    //av_dump_format(avfctx_, 0, input_file, 0);

    for(unsigned i=0; i<avfctx_->nb_streams; i++)
    {
        if(enable_video_ && avfctx_->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {

            if((avfctx_->streams[i]->codec->codec_id == CODEC_ID_MPEG2VIDEO)
                    ||(avfctx_->streams[i]->codec->codec_id == CODEC_ID_H264)
                    ||(avfctx_->streams[i]->codec->codec_id == AV_CODEC_ID_HEVC)
              )
            {
                if(stream_index_video_ != -1) {
                    printf("ignore second video, codec id = %d, stream index = %d\n",
                            avfctx_->streams[i]->codec->codec_id, i);
                    continue;
                }

                stream_index_video_ = i;
                video_width_ = avfctx_->streams[i]->codec->width;
                video_height_ = avfctx_->streams[i]->codec->height;
                AVCodecContext * codec_context = avfctx_->streams[i]->codec;
                video_config_ = (video_stream_config *) malloc(sizeof(video_stream_config));
                video_config_->video_encoding = codec_context->codec_id;
                video_config_->framerate = av_time_base_to_omx_framerate(codec_context->time_base);
                video_config_->width = codec_context->width;
                video_config_->height = codec_context->height;
                video_config_->extrasize = codec_context->extradata_size;
                video_config_->extradata = malloc(codec_context->extradata_size);
                memcpy(video_config_->extradata, codec_context->extradata, codec_context->extradata_size);
                printf("video : %d x %d, stream index = %d\n", video_width_, video_height_, i);
            }

            switch(avfctx_->streams[i]->codec->codec_id) {
                case CODEC_ID_MPEG2VIDEO:
                    video_codec_ = NDL_ESP_VIDEO_CODEC_H262;
                    break;
                case CODEC_ID_H264:
                    video_codec_ = NDL_ESP_VIDEO_CODEC_H264;
                    break;
                case AV_CODEC_ID_HEVC:
                    video_codec_ = NDL_ESP_VIDEO_CODEC_H265;
                    break;
                default:
                    fprintf(stderr, "unsupported video codec_id:%d\n",
                            avfctx_->streams[i]->codec->codec_id);
                    break;
            }
        }
        else if(enable_audio_ && avfctx_->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            if((avfctx_->streams[i]->codec->codec_id == CODEC_ID_MP2)
                    ||(avfctx_->streams[i]->codec->codec_id == CODEC_ID_MP3)
                    ||(avfctx_->streams[i]->codec->codec_id == CODEC_ID_AC3)
                    ||(avfctx_->streams[i]->codec->codec_id == CODEC_ID_EAC3)
                    ||(avfctx_->streams[i]->codec->codec_id == CODEC_ID_AAC)
                    ||(avfctx_->streams[i]->codec->codec_id == CODEC_ID_AAC_LATM)
                    ||(avfctx_->streams[i]->codec->codec_id == AV_CODEC_ID_PCM_S16LE)
              )
            {
                if(stream_index_audio_ != -1) {
                    printf("ignore second audio, codec id = %d, stream index = %d\n",
                            avfctx_->streams[i]->codec->codec_id, i);
                    continue;
                }

                stream_index_audio_ = i;

                AVCodecContext * codec_context = avfctx_->streams[i]->codec;
                audio_config_ = (audio_stream_config *) malloc(sizeof(audio_stream_config));
                audio_config_->channels      = codec_context->channels;
                audio_config_->samplerate    = codec_context->sample_rate;
                audio_config_->blockalign    = codec_context->block_align;
                audio_config_->bitrate       = codec_context->bit_rate;
                audio_config_->bitspersample = codec_context->bits_per_coded_sample;
                if (audio_config_->bitspersample == 0)
                    audio_config_->bitspersample = 16;
                printf("Audio channel:%d, samplerate:%d, align:%d, bitrate:%d, bitper:%d\n",
                        audio_config_->channels,
                        audio_config_->samplerate,
                        audio_config_->blockalign,
                        audio_config_->bitrate,
                        audio_config_->bitspersample);
                printf("audio codec id = %d, stream index = %d\n", avfctx_->streams[i]->codec->codec_id, i);
            }

            switch(avfctx_->streams[i]->codec->codec_id) {
                case CODEC_ID_MP2:
                    audio_codec_ = NDL_ESP_AUDIO_CODEC_MP2;
                    break;
                case CODEC_ID_MP3:
                    audio_codec_ = NDL_ESP_AUDIO_CODEC_MP3;
                    break;
                case CODEC_ID_AC3:
                    audio_codec_ = NDL_ESP_AUDIO_CODEC_AC3;
                    break;
                case CODEC_ID_EAC3:
                    audio_codec_ = NDL_ESP_AUDIO_CODEC_EAC3;
                    break;
                case CODEC_ID_AAC:
                    audio_codec_ = NDL_ESP_AUDIO_CODEC_AAC;
                    break;
                case CODEC_ID_AAC_LATM:
                    audio_codec_ = NDL_ESP_AUDIO_CODEC_HEAAC;
                    break;
                case AV_CODEC_ID_PCM_S16LE:
                    audio_codec_ = NDL_ESP_AUDIO_CODEC_PCM_44100_2CH;
                    break;
                default:
                    fprintf(stderr, "unsupported audio codec_id:%d\n",
                            avfctx_->streams[i]->codec->codec_id);
                    break;
            }
        }

    }

    //av_init_packet(&avpkt_);
    return (stream_index_video_>=0) || (stream_index_audio_>=0);
}

const std::shared_ptr<Frame> FfmpegReader::getFrame(NDL_ESP_STREAM_T type)
{
    if(type == NDL_ESP_AUDIO_ES)
        return getAudio();

    return getVideo();
}

void FfmpegReader::seekTo(int64_t to)
{
    {
        std::lock_guard<std::mutex> lock(stream_queue_lock_);
        if (avfctx_){
            av_seek_frame(avfctx_, -1, to, AVSEEK_FLAG_BACKWARD);
        }

        end_of_video_ = false;
        end_of_audio_ = false;

        if(!audio_.empty())
        {
            FrameQueue empty;
            std::swap(audio_, empty);
        }
        if(!video_.empty())
        {
            FrameQueue empty;
            std::swap(video_, empty);
        }
    }
}

int64_t FfmpegReader::getDuration()
{
    if (avfctx_) {
        return avfctx_->duration;
    }
    return 0;
}

void FfmpegReader::popFrame(NDL_ESP_STREAM_T type)
{
    {
        std::lock_guard<std::mutex> lock(stream_queue_lock_);
        if(type == NDL_ESP_AUDIO_ES) {
            if(!audio_.empty())
                audio_.pop();
        }
        else {
            if(!video_.empty())
                video_.pop();
        }
    }
}

const std::shared_ptr<Frame> FfmpegReader::getAudio()
{
    std::shared_ptr<Frame> ret;
    {
        std::lock_guard<std::mutex> lock(stream_queue_lock_);
        if(audio_.empty())
            ret = getByStreamIndex(stream_index_audio_, audio_,
                    stream_index_video_, video_);
        else
            ret = audio_.front();
    }

    if(!ret && !end_of_audio_) {
        // generate EOS frame
        end_of_audio_ = true;
        ret = std::make_shared<Frame>(NDL_ESP_AUDIO_ES);
        audio_.push(ret);
        printf("last audio frame!!!!\n");
    }
    return ret;
}

const std::shared_ptr<Frame> FfmpegReader::getVideo()
{
    std::shared_ptr<Frame> ret;
    {
        std::lock_guard<std::mutex> lock(stream_queue_lock_);
        if(video_.empty())
            ret = getByStreamIndex(stream_index_video_, video_,
                    stream_index_audio_, audio_);
        else
            ret = video_.front();
    }

    if(!ret && !end_of_video_) {
        // generate EOS frame
        end_of_video_ = true;
        ret = std::make_shared<Frame>(NDL_ESP_VIDEO_ES);
        video_.push(ret);
        printf("last video frame!!!!\n");
    }
    return ret;
}

const std::shared_ptr<Frame> FfmpegReader::getByStreamIndex(
        int match_index,
        FrameQueue& match_queue,
        int mismatch_index,
        FrameQueue& mismatch_queue)
{

    int err;
    while ((err = av_read_frame(avfctx_, &avpkt_)) >= 0) {

        int64_t pts = 0;

        pts = (1000000 * avpkt_.pts * avfctx_->streams[match_index]->time_base.num) /
            avfctx_->streams[match_index]->time_base.den;

        auto frame = std::make_shared<Frame>( getTypeByStreamIndex(avpkt_.stream_index),
                pts,
                avpkt_.data,
                avpkt_.size
                );

        if (avpkt_.stream_index == match_index) {
            match_queue.push(frame);
            av_free_packet(&avpkt_);
            return frame;
        }

        if (avpkt_.stream_index == mismatch_index) {
            mismatch_queue.push(frame);
            if(mismatch_queue.size() >= MAX_QUEUE_SIZE) {
                printf("too many packets are queued !!!!");
            }
        }

        av_free_packet(&avpkt_);
    }
    return 0;
}

