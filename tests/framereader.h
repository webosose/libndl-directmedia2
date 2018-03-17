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

#ifndef TESTS_FRAME_READER_H_
#define TESTS_FRAME_READER_H_

#include "ndl-directmedia2/esplayer-api.h"
#include <memory>
#include <queue>
#include <string.h>

typedef struct video_stream_config_t {
    uint32_t video_encoding;
    uint32_t framerate;
    uint32_t width;
    uint32_t height;
    void *extradata;
    uint32_t extrasize;
} video_stream_config;

typedef struct audio_stream_config_t {
    uint32_t channels;
    uint32_t samplerate;
    uint32_t blockalign;
    uint32_t bitrate;
    uint32_t bitspersample;
} audio_stream_config;

class Frame : public NDL_ESP_STREAM_BUFFER {
    public:
        Frame(NDL_ESP_STREAM_T type,

                int64_t pts,
                uint8_t* buf, uint32_t size)
        {
            data = new uint8_t[size];
            data_len = size;
            offset = 0;
            stream_type = type;
            timestamp = pts;
            flags = 0;
            memcpy(data, buf, size);
        }

        explicit Frame(NDL_ESP_STREAM_T type) {
            //end of frame
            data_len = 0;
            data = nullptr;
            offset = 0;
            stream_type = type;
            timestamp = 0;
            flags = NDL_ESP_FLAG_END_OF_STREAM;
        }
        ~Frame() {
            delete []  data;
        }
};


class FrameReader {
    public:
        virtual ~FrameReader() {}

        virtual bool init(const char* input_file) = 0;

        virtual bool contains(NDL_ESP_STREAM_T type) = 0;

        virtual NDL_ESP_VIDEO_CODEC getVideoCodec() = 0;
        virtual NDL_ESP_AUDIO_CODEC getAudioCodec() = 0;
        virtual void setForcedVideoCodec(NDL_ESP_VIDEO_CODEC c) = 0;
        virtual void setForcedAudioCodec(NDL_ESP_AUDIO_CODEC c) = 0;

        virtual void seekTo(int64_t to) = 0;
        virtual int64_t getDuration(void) { return -1; }
        virtual video_stream_config* getVideoStreamConfig() = 0;
        virtual audio_stream_config* getAudioStreamConfig() = 0;
        virtual bool isEnded(NDL_ESP_STREAM_T type) = 0;
        virtual const std::shared_ptr<Frame> getFrame(NDL_ESP_STREAM_T type) = 0;
        virtual void popFrame(NDL_ESP_STREAM_T type) = 0;

        using FrameQueue = std::queue<std::shared_ptr<Frame>>;
};



#endif //#ifndef TESTS_FRAME_READER_H_
