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

#ifndef TESTS_FFMPEG_READER_H_
#define TESTS_FFMPEG_READER_H_

#include "ndl-directmedia2/esplayer-api.h"
#include "framereader.h"

#include <queue>
#include <memory>
#include <string.h>
#include <mutex>

extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
}

class FfmpegReader : public FrameReader {

    public:
        FfmpegReader(bool video, bool audio);
        ~FfmpegReader();

        bool init(const char* input_file);

        bool contains(NDL_ESP_STREAM_T type) {
            if(type == NDL_ESP_AUDIO_ES)
                return stream_index_audio_ != -1;
            return stream_index_video_ != -1;
        }

        bool isEnded(NDL_ESP_STREAM_T type) {
            if(type == NDL_ESP_VIDEO_ES)
                return end_of_video_;
            return end_of_audio_;
        }
        const std::shared_ptr<Frame> getFrame(NDL_ESP_STREAM_T type);
        void popFrame(NDL_ESP_STREAM_T type);

        void seekTo(int64_t to);
        int64_t getDuration(void);

        video_stream_config* getVideoStreamConfig() { return video_config_; }
        audio_stream_config* getAudioStreamConfig() { return audio_config_; }

        NDL_ESP_VIDEO_CODEC getVideoCodec() { return video_codec_; }
        NDL_ESP_AUDIO_CODEC getAudioCodec() { return audio_codec_; }

        // override audio/video codec for test
        void setForcedVideoCodec(NDL_ESP_VIDEO_CODEC c) {video_codec_ = c;}
        void setForcedAudioCodec(NDL_ESP_AUDIO_CODEC c) {audio_codec_ = c;}

    private:

        const std::shared_ptr<Frame> getAudio();
        const std::shared_ptr<Frame> getVideo();
        const std::shared_ptr<Frame> getByStreamIndex(
                int match_index,
                FrameQueue& match_queue,
                int mismatch_index,
                FrameQueue& mismatch_queue);

        NDL_ESP_STREAM_T getTypeByStreamIndex(int index) {
            if(index == stream_index_video_)
                return NDL_ESP_VIDEO_ES;
            return NDL_ESP_AUDIO_ES;
        }

        enum {
            MAX_QUEUE_SIZE = 10000,  // no error handling, just output warning message.
        };

    private:
        bool enable_video_ {false};
        bool enable_audio_ {false};

        bool end_of_video_ {false};
        bool end_of_audio_ {false};

        AVFormatContext *avfctx_ {nullptr};
        AVPacket avpkt_;

        NDL_ESP_VIDEO_CODEC video_codec_ {NDL_ESP_VIDEO_NONE};
        NDL_ESP_AUDIO_CODEC audio_codec_ {NDL_ESP_AUDIO_NONE};

        int stream_index_video_ {-1};
        int stream_index_audio_ {-1};

        int video_width_ {0};
        int video_height_ {0};

        FrameQueue video_;
        FrameQueue audio_;

        std::mutex stream_queue_lock_;
        video_stream_config *video_config_;
        audio_stream_config *audio_config_;
};

#endif // #ifndef TESTS_FFMPEG_READER_H_
