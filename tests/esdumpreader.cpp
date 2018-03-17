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

#include "esdumpreader.h"

#include <assert.h>
#include <fcntl.h>
#include <algorithm>
#include <memory>



EsDumpReader::EsDumpReader(bool video, bool audio)
: enable_video_(video), enable_audio_(audio)
{
}

bool EsDumpReader::init(const char* input_path)
{
    char es[256];
    char pts[256];
    char info[256];


    if(enable_audio_) {
        sprintf(es, "%s/audio-es.bin", input_path);
        sprintf(pts, "%s/pts-audio.bin", input_path);
        es_dump_[AUDIO].init(NDL_ESP_AUDIO_ES, fopen(es, "r"), fopen(pts, "r"));

        // load codec info from audio-info.txt
        int codec = 1;
        int split = 1;
        sprintf(info, "%s/audio-info.txt", input_path);
        if(parse_info(info, &codec, &split)) {
            printf("setting audio_codec_ : %d\n", codec);
            audio_codec_ = (NDL_ESP_AUDIO_CODEC)codec;

            // if need to split frames, compute PTS gap from the split count
            if(split > 1) {
                es_dump_[AUDIO].set_split(split);
            }
        }
    }

    if(enable_video_) {
        sprintf(es, "%s/video-es.bin", input_path);
        sprintf(pts, "%s/pts-video.bin", input_path);
        sprintf(info, "%s/video-info.txt", input_path);
        es_dump_[VIDEO].init(NDL_ESP_VIDEO_ES, fopen(es, "r"), fopen(pts, "r"));

        // load codec info from video-info.txt
        int codec = 1;
        sprintf(info, "%s/video-info.txt", input_path);
        if(parse_info(info, &codec, nullptr)) {
            printf("setting video_codec_ : %d\n", codec);
            video_codec_ = (NDL_ESP_VIDEO_CODEC)codec;
        }
    }

    return es_dump_[AUDIO].valid() || es_dump_[VIDEO].valid();
}

bool EsDumpReader::parse_info(const char* info, int* codec, int* split)
{
    FILE* f = fopen(info, "r");
    if(!f) {
        printf("parse info : %s not found\n", info);
        return false;
    }

    char buf[128];
    bool result = false;
    while(!feof(f) && fgets(buf, sizeof(buf), f)) {
        if(buf[0] == '#')
            continue;

        int first=0, second=0;
        int count = sscanf(buf, "%d %d", &first, &second);
        if(count > 0 && codec)
            *codec = first;
        if(count > 1 && split)
            *split = second;

        if(count > 0)
            result = true;
        break;
    }
    fclose(f);
    return result;
}

EsDumpReader::EsDump::~EsDump()
{
    if(es_) {
        fclose(es_);
        es_ = nullptr;
    }

    if(pts_) {
        fclose(pts_);
        pts_ = nullptr;
    }
}
void EsDumpReader::EsDump::set_split(int split)
{
    if(!pts_)
        return;

    split_count_ = split;

    int dummy = 0;
    int pts_diff = 0;
    int64_t pts_first = 0;
    int64_t pts_second = 0;

    long pos = ftell(pts_);
    if((fscanf(pts_, "%lld %d", &pts_first, &dummy) == 2)
            && (fscanf(pts_, "%lld %d", &pts_second, &dummy) == 2)) {

        pts_diff = (pts_second - pts_first) / split;
        printf("will split every audio packets into %d frames, "
                "and pts difference between each frame is %d \n", split, pts_diff);
    }
    fseek(pts_, pos, SEEK_SET);
}

void EsDumpReader::EsDump::init(NDL_ESP_STREAM_T type, FILE* es, FILE* pts)
{
    es_ = es;
    pts_ = pts;
    type_ = type;
}

void EsDumpReader::EsDump::seekTo(int64_t to)
{
    printf("ERROR: %s is not implemented.\n", __func__);
}

const std::shared_ptr<Frame> EsDumpReader::EsDump::getFrame()
{
    assert(valid());

    if(!queue_.empty())
        return queue_.front();

    int64_t pts;
    uint32_t size;

    std::shared_ptr<Frame> ret;

    if(fscanf(pts_, "%lld %d", &pts, &size) == 2) {
        uint8_t* buf = new uint8_t[size];
        if (size == 0) goto EOS;
        end_of_dump_ = false;
        if(fread(buf, 1, size, es_) == size) {
            for(int i=0; i<split_count_; ++i) {
                ret = std::make_shared<Frame>(
                        type_,
                        pts+i*pts_diff_,
                        buf+i*size/split_count_,
                        size/split_count_);
                queue_.push(ret);
            }
            delete [] buf;
            return queue_.front();
        }
        delete [] buf;
    }
EOS:
    if(!end_of_dump_) {
        // generate EOS frame
        end_of_dump_ = true;
        ret = std::make_shared<Frame>(type_);
        queue_.push(ret);
        printf("last frame type:%d !!!!\n", type_);
    }
    return ret;

}

void EsDumpReader::EsDump::popFrame()
{
    if(!queue_.empty())
        queue_.pop();
}
