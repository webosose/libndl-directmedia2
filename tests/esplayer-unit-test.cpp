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

#include <iostream>
#include <gtest/gtest.h>
#include <array>
#include <execinfo.h>
#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include "esdumpreader.h"
#include "ffmpegreader.h"
#include "framereader.h"
#include "ndl-directmedia2/esplayer-api.h"

#define FEEDING_INTERVAL 300000000 // 300 ms
#define LOOP_COUNT  60  // run 60 seconds..
#define LOG_PER_FEED  60

#define UNITTEST_PRECONDITION_LOAD { \
    player = NDL_EsplayerCreate("com.webos.app.ndl.unit.test",::esplayer_callback, this); \
    NDL_EsplayerGetConnectionId(player, connectionId, sizeof(connectionId)); \
    NDL_EsplayerSetAppForegroundState(player, NDL_ESP_APP_STATE_FOREGROUND); \
    NDL_EsplayerSetVideoDisplayWindow(player, 0, 0, 1920, 1080, 1); \
    NDL_EsplayerLoad(player, &metadata); \
} \

//if UNITTEST_PRECONDITION_FEED is called, UNITEST_POSTCONDITION_FEED has to call in TEST_F function.
#define UNITTEST_PRECONDITION_FEED { \
    pthread_mutex_init(&event_lock, NULL); \
    pthread_condattr_init(&event_attr); \
    pthread_condattr_setclock(&event_attr , CLOCK_MONOTONIC); \
    pthread_cond_init(&event_cond, &event_attr); \
    pthread_create(&event_handler_thread, NULL, ::feeding_thread, this);\
} \

#define UNITTEST_POSTCONDITION_FEED { \
    quit = 1; \
    pthread_join(event_handler_thread,  NULL); \
} \

/* this code is testing. so this sleep is just check for omx callback from each platform.*/
#define UNITTEST_PRECONDITION_PLAY { \
    if(framereader->contains(NDL_ESP_VIDEO_ES) || \
            framereader->contains(NDL_ESP_AUDIO_ES)) { \
        printf("player loaded -> need more data\n"); \
        need_more_data = true; \
    } \
    pthread_cond_signal(&event_cond); \
    NDL_EsplayerPlay(player); \
    sleep(1); \
} \

#define UNITTEST_PRECONDITION_UNLOAD { \
    NDL_EsplayerUnload(player); \
} \

using namespace std;

// The fixture for testing class
class esplayer_unit_test : public ::testing::Test {

    protected:
        virtual void SetUp();
        virtual void TearDown();
    public:
        NDL_EsplayerHandle player;
        FrameReader* framereader = nullptr;
        NDL_ESP_META_DATA metadata;

        char* input_file ="/tmp/usb/sda/sda1/test.ts";
        char connectionId[CONNECTION_ID_BUFFER_SIZE] = "";
        int quit = 0;
        int no_video = 0;
        int no_audio = 0;
        int repeat_forever = 0;
        int forced_video_codec = -1;
        int forced_audio_codec = -1;
        int input_file_count = 1;
        int result = 0;
        int64_t duration = 0;
        int64_t play_until = 0; //nanoseconds

        bool flushing = false;
        bool need_more_data = false;
        bool videomute = false;
        bool audiomute = false;
        bool trickmode = false;
        bool quit_all = false;

        pthread_t event_handler_thread;
        // mutex for feeding thread
        pthread_mutex_t event_lock;
        pthread_cond_t event_cond;
        pthread_condattr_t event_attr;

        void init_input(const char* input_file);
        void esplayer_callback(NDL_ESP_EVENT event, void* playerdata);
        void* feeding_thread();
        int feed_frame(NDL_ESP_STREAM_T type);
};

void esplayer_callback(NDL_ESP_EVENT event, void* playerdata, void* userdata) {
    esplayer_unit_test* unit = (esplayer_unit_test*) userdata;
    unit->esplayer_callback(event, playerdata);
}

void esplayer_unit_test::esplayer_callback(NDL_ESP_EVENT event, void* playerdata) {
    switch(event) {
        case NDL_ESP_FIRST_FRAME_PRESENTED: {
                                                printf("%s NDL_ESP_FIRST_FRAME_PRESENTED\n", __FUNCTION__);
                                                break;
                                            }
        case NDL_ESP_LOW_THRESHOLD_CROSSED_VIDEO:
                                            pthread_mutex_lock(&event_lock);
                                            need_more_data = true;
                                            pthread_mutex_unlock(&event_lock);
                                            pthread_cond_signal(&event_cond);
                                            break;
        case NDL_ESP_LOW_THRESHOLD_CROSSED_AUDIO:
                                            pthread_mutex_lock(&event_lock);
                                            need_more_data = true;
                                            pthread_mutex_unlock(&event_lock);
                                            pthread_cond_signal(&event_cond);
                                            break;
        case NDL_ESP_STREAM_DRAINED_VIDEO: {
                                               printf("stream drained !!!\n");
                                               break;
                                           }
        case NDL_ESP_STREAM_DRAINED_AUDIO: {
                                               printf("stream drained audio !!!\n");
                                               break;
                                           }
        case NDL_ESP_END_OF_STREAM: {
                                        printf("end of stream !!!\n");
                                        quit = 1;
                                        pthread_cond_signal(&event_cond);
                                        break;
                                    }
        case NDL_ESP_RESOURCE_RELEASED_BY_POLICY: {
                                                      printf("%s:%d, receive policy action event.\n", __func__, __LINE__);
                                                      break;
                                                  }
        case NDL_ESP_VIDEO_INFO: {
                                     NDL_ESP_VIDEO_INFO_T* data = (NDL_ESP_VIDEO_INFO_T*)playerdata;
                                     printf("[Test] Receive video resolution. width : %d, height : %d\n", data->width, data->height);
                                     break;
                                 }
    }
}

int esplayer_unit_test::feed_frame(NDL_ESP_STREAM_T type)
{
    static int feed_count = 0;

    std::shared_ptr<Frame> frame = framereader->getFrame(type);
    if(!frame) {
        printf("error in getting new frames....%d frames are fed\n", feed_count);
        return -1;
    }

    if(!player) {
        printf("player is destroyed...\n");
        return -1;
    }
    int result = NDL_EsplayerFeedData(player, frame.get());
    if(result>=0) {
        frame->offset+=result;
        if (frame->data_len == frame->offset) {
            feed_count += 1;
            framereader->popFrame(type);

            if((type == NDL_ESP_VIDEO_ES) && (feed_count%LOG_PER_FEED == 0) ) {
                printf("%d frames are fed..\n", feed_count);
            }
        }
        return result;
    }

    return -1;
}

inline int64_t current_time_ns()
{
    timespec t;
    if(clock_gettime(CLOCK_MONOTONIC, &t) == 0) {
        int64_t ns = t.tv_sec * 1000000000LL + t.tv_nsec;
        return ns;
    }
    return -1;
}

static void* feeding_thread(void* arg)
{
    esplayer_unit_test* unit = (esplayer_unit_test*)arg;
    return unit->feeding_thread();
}

void* esplayer_unit_test::feeding_thread()
{
    int audio_written_size = 0;
    int video_written_size = 0;
    timespec ts;

    if (!framereader->contains(NDL_ESP_AUDIO_ES)) audio_written_size = -1;
    if (!framereader->contains(NDL_ESP_VIDEO_ES)) video_written_size = -1;

    while(!quit)
    {
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        {
            printf( "system : clock_gettime error...... \n");
            goto END;
        }
        ts.tv_nsec += FEEDING_INTERVAL;

        pthread_mutex_lock(&event_lock);
        pthread_cond_timedwait(&event_cond, &event_lock, &ts);

        if (need_more_data && !flushing) {
            do {
                if (video_written_size >= 0 && framereader->contains(NDL_ESP_VIDEO_ES))
                    video_written_size = feed_frame(NDL_ESP_VIDEO_ES);
                if (audio_written_size >= 0 && framereader->contains(NDL_ESP_AUDIO_ES))
                    audio_written_size = feed_frame(NDL_ESP_AUDIO_ES);
            } while (!quit&& (audio_written_size >= 0 || video_written_size >= 0));
            need_more_data = false;
            if (framereader->contains(NDL_ESP_AUDIO_ES)) audio_written_size =0;
            if (framereader->contains(NDL_ESP_VIDEO_ES)) video_written_size =0;
        }
        pthread_mutex_unlock(&event_lock);

        if (play_until && (play_until < current_time_ns())) {
            printf( "%s quit by duration\n", __func__);
            quit = true;
        }
    }
END:
    pthread_exit((void*)0);
}

void esplayer_unit_test::SetUp()
{
    init_input(input_file);
    metadata.audio_codec = framereader->getAudioCodec();
    metadata.video_codec = framereader->getVideoCodec();
}

void esplayer_unit_test::TearDown()
{
    if (player)
        NDL_EsplayerDestroy(player);
    player = 0;
    result = 0;
    delete framereader;
    framereader = nullptr;
}

void esplayer_unit_test::init_input(const char* input_file)
{
    struct stat sb;
    if(stat(input_file, &sb) == -1) {
        perror("stat");
        exit(-1);
    }

    if(S_ISREG(sb.st_mode)) {
        printf("using input file: %s\n", input_file);
        framereader = new FfmpegReader(no_video?false:true, no_audio?false:true);
    } else if(S_ISDIR(sb.st_mode)) {
        printf("load ES dump files from : %s\n", input_file);
        framereader = new EsDumpReader(no_video?false:true, no_audio?false:true);
    } else {
        exit(-1);
    }

    if(!framereader->init(input_file)) {
        printf("error in framefeeder.init()\n");
        exit(-1);
    }

    if(framereader->contains(NDL_ESP_VIDEO_ES) && (forced_video_codec>0)) {
        printf("force video codec to %d instead of %d\n",
                forced_video_codec, framereader->getVideoCodec());
        framereader->setForcedVideoCodec((NDL_ESP_VIDEO_CODEC)forced_video_codec);
    }
    if(framereader->contains(NDL_ESP_AUDIO_ES) && (forced_audio_codec>0)) {
        printf("force audio codec to %d instead of %d\n",
                forced_audio_codec, framereader->getAudioCodec());
        framereader->setForcedAudioCodec((NDL_ESP_AUDIO_CODEC)forced_audio_codec);
    }
}

TEST_F(esplayer_unit_test, NDL_EsplayerCreate)
{
    player = NDL_EsplayerCreate("com.webos.app.ndl.unit.test",::esplayer_callback, (void*)this);
    ASSERT_TRUE(player);
}

TEST_F(esplayer_unit_test, NDL_EsplayerGetConnectionId)
{
    player = NDL_EsplayerCreate("com.webos.app.ndl.unit.test",::esplayer_callback, this);

    result = NDL_EsplayerGetConnectionId(player, connectionId, sizeof(connectionId));
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerSetAppForegroundState)
{
    player = NDL_EsplayerCreate("com.webos.app.ndl.unit.test",::esplayer_callback, this);

    result = NDL_EsplayerSetAppForegroundState(player, NDL_ESP_APP_STATE_FOREGROUND);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerSetVideoDisplayWindow)
{
    player = NDL_EsplayerCreate("com.webos.app.ndl.unit.test",::esplayer_callback, this);
    NDL_EsplayerSetAppForegroundState(player, NDL_ESP_APP_STATE_FOREGROUND);

    result = NDL_EsplayerSetVideoDisplayWindow(player, 0, 0, 1920, 1080, 1);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerSetVideoCustomDisplayWindow)
{
    player = NDL_EsplayerCreate("com.webos.app.ndl.unit.test",::esplayer_callback, this);
    NDL_EsplayerSetAppForegroundState(player, NDL_ESP_APP_STATE_FOREGROUND);

    result = NDL_EsplayerSetVideoCustomDisplayWindow(player, 0, 0, 1920, 1080, 0, 0, 1920, 1080, 1);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerLoad)
{
    player = NDL_EsplayerCreate("com.webos.app.ndl.unit.test",::esplayer_callback, this);
    NDL_EsplayerSetAppForegroundState(player, NDL_ESP_APP_STATE_FOREGROUND);
    NDL_EsplayerSetVideoDisplayWindow(player, 0, 0, 1920, 1080, 1);

    result = NDL_EsplayerLoad(player, &metadata);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerPlay)
{
    UNITTEST_PRECONDITION_LOAD;

    result = NDL_EsplayerPlay(player);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerFlush)
{
    UNITTEST_PRECONDITION_LOAD;
    result = NDL_EsplayerFlush(player);

    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerPause)
{
    UNITTEST_PRECONDITION_LOAD;

    result = NDL_EsplayerPause(player);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerStepFrame)
{
    UNITTEST_PRECONDITION_LOAD;
    UNITTEST_PRECONDITION_FEED;
    UNITTEST_PRECONDITION_PLAY
        NDL_EsplayerPause(player);

    result = NDL_EsplayerStepFrame(player);
    UNITTEST_POSTCONDITION_FEED;
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerMuteAudio)
{
    UNITTEST_PRECONDITION_LOAD;
    audiomute = !audiomute;

    result = NDL_EsplayerMuteAudio(player, audiomute);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerMuteVideo )
{
    UNITTEST_PRECONDITION_LOAD;
    videomute = !videomute;

    result = NDL_EsplayerMuteVideo(player, videomute);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test,NDL_EsplayerSetTrickMode)
{
    UNITTEST_PRECONDITION_LOAD;
    trickmode = !trickmode;

    result = NDL_EsplayerSetTrickMode(player, trickmode);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test,NDL_EsplayerSetPlaybackRate)
{
    UNITTEST_PRECONDITION_LOAD;

    result = NDL_EsplayerSetPlaybackRate(player, 1000);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test,NDL_EsplayerReloadAudio)
{
    UNITTEST_PRECONDITION_LOAD;

    result = NDL_EsplayerReloadAudio(player, &metadata);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerSet3DType)
{
    UNITTEST_PRECONDITION_LOAD;

    result = NDL_EsplayerSet3DType(player, E3DTYPE_CHECKERBOARD);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test,NDL_EsplayerUnload)
{
    UNITTEST_PRECONDITION_LOAD;

    result = NDL_EsplayerUnload(player);
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test,NDL_EsplayerFeedData)
{
    UNITTEST_PRECONDITION_LOAD;
    std::shared_ptr<Frame> frame = framereader->getFrame(NDL_ESP_VIDEO_ES);

    result = NDL_EsplayerFeedData(player, frame.get());
    ASSERT_LE(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerGetStatus)
{
    UNITTEST_PRECONDITION_LOAD;

    if (NDL_ESP_STATUS_LOADED == NDL_EsplayerGetStatus(player))
        result = NDL_ESP_RESULT_SUCCESS;
    else
        result = NDL_ESP_RESULT_FAIL;
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test, NDL_EsplayerGetMediatime)
{
    UNITTEST_PRECONDITION_LOAD;
    UNITTEST_PRECONDITION_FEED;
    UNITTEST_PRECONDITION_PLAY;

    int64_t startTime=0;
    int64_t currTime=0;
    result = NDL_EsplayerGetMediatime(player, &startTime, &currTime);
    UNITTEST_POSTCONDITION_FEED;
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test,NDL_EsplayerGetBufferLevel)
{
    UNITTEST_PRECONDITION_LOAD;
    UNITTEST_PRECONDITION_FEED;
    UNITTEST_PRECONDITION_PLAY;

    int level = 0;
    result = NDL_EsplayerGetBufferLevel(player, NDL_ESP_VIDEO_ES, (uint32_t*)&level);
    UNITTEST_POSTCONDITION_FEED;
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

TEST_F(esplayer_unit_test,NDL_EsplayerDestroy)
{
    UNITTEST_PRECONDITION_LOAD;
    UNITTEST_PRECONDITION_UNLOAD;

    NDL_EsplayerDestroy(player);
    player = 0;
    ASSERT_EQ(NDL_ESP_RESULT_SUCCESS, result);
}

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
