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

#ifndef AUDIO_SW_DECODER_H_
#define AUDIO_SW_DECODER_H_

extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
}

namespace NDL_Esplayer {

    typedef struct AudioStreamInfo {
        enum AVCodecID codec_id;
        int            channels;
        int            sample_rate;
        int64_t        bit_rate;
        int            block_align;
        int            bits_per_coded_sample;
    } AudioStreamInfo;

    class AudioSwDecoder {
        public:
            AudioSwDecoder();
            ~AudioSwDecoder();
            int GetOutputBufferSize() { return output_buffer_size_; };
            unsigned char* GetOutputBufferData() { return output_buffer_data_; };
            bool OpenAudio(enum AVCodecID codec_id);
            int DecodeAudio(unsigned char* data, int size, double dts, double pts);

        public:
            AudioStreamInfo audio_stream_info_;

        private:
            AVCodecContext* avctx_;
            AVFrame* avframe_;

            SwrContext* swrctx_;
            enum AVSampleFormat input_sample_fmt_;
            enum AVSampleFormat output_sample_fmt_;

            int output_channels_;
            int output_buffer_size_;
            unsigned char* output_buffer_data_;
    };

}
#endif // #ifndef AUDIO_SW_DECODER_H_
