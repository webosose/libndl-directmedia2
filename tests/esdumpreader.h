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

#ifndef TESTS_ESDUMP_READER_H_
#define TESTS_ESDUMP_READER_H_


#include "ndl-directmedia2/esplayer-api.h"
#include "framereader.h"


class EsDumpReader : public FrameReader {
    public:
        EsDumpReader(bool video, bool audio);

        virtual bool init(const char* input_path);

        virtual bool contains(NDL_ESP_STREAM_T type) {
            return es_dump_[type == NDL_ESP_AUDIO_ES ? AUDIO : VIDEO].valid();
        }

        virtual NDL_ESP_VIDEO_CODEC getVideoCodec() { return video_codec_; }
        virtual NDL_ESP_AUDIO_CODEC getAudioCodec() { return audio_codec_; }

        // override audio/video codec for test
        void setForcedVideoCodec(NDL_ESP_VIDEO_CODEC c) {video_codec_ = c;}
        void setForcedAudioCodec(NDL_ESP_AUDIO_CODEC c) {audio_codec_ = c;}
        video_stream_config* getVideoStreamConfig() { return video_config_; }
        audio_stream_config* getAudioStreamConfig() { return audio_config_; }

        virtual void seekTo(int64_t to) {
            for(auto& dump : es_dump_)
                if(dump.valid())
                    dump.seekTo(to);
        }

        virtual bool isEnded(NDL_ESP_STREAM_T type) {
            return es_dump_[type == NDL_ESP_AUDIO_ES ? AUDIO : VIDEO].isEnded();
        }

        virtual const std::shared_ptr<Frame> getFrame(NDL_ESP_STREAM_T type) {
            EsDump& dump = es_dump_[type == NDL_ESP_AUDIO_ES ? AUDIO : VIDEO];
            return dump.getFrame();
        }
        virtual void popFrame(NDL_ESP_STREAM_T type) {
            es_dump_[type == NDL_ESP_AUDIO_ES ? AUDIO : VIDEO].popFrame();
        }

    private:
        bool parse_info(const char* info, int* codec, int* split);

    private:
        class EsDump {
            public:
                ~EsDump();

                void init(NDL_ESP_STREAM_T type, FILE* es, FILE* pts);
                void set_split(int split);

                bool valid() {
                    return es_ && pts_;
                }

                void seekTo(int64_t to);
                bool isEnded() {return end_of_dump_;}
                const std::shared_ptr<Frame> getFrame();
                void popFrame();

            private:
                FILE* es_ {nullptr};   // "audio-es.bin" or "video-es.bin"
                FILE* pts_ {nullptr};   // "pts-audio.bin" or "pts-video.bin"
                NDL_ESP_STREAM_T type_;

                bool end_of_dump_ {false};

                // packets in pts-audio.bin can contain multiple audio frames.
                //   if split_count is specified in "audio-info.txt"
                //   split audio frames into single frames
                //
                int split_count_ {1};
                int pts_diff_ {0};

                FrameQueue queue_;
        };

        enum ES_INDEX {
            AUDIO = 0,
            VIDEO,
        };

    private:
        EsDump es_dump_[2];
        bool enable_video_ {false};
        bool enable_audio_ {false};

        NDL_ESP_VIDEO_CODEC video_codec_ {NDL_ESP_VIDEO_NONE};
        NDL_ESP_AUDIO_CODEC audio_codec_ {NDL_ESP_AUDIO_NONE};
        video_stream_config* video_config_;
        audio_stream_config* audio_config_;
};


#endif // #ifndef TESTS_ESDUMP_READER_H_
