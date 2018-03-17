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
#include "lunaservicecall.h"
#include "ndl-directmedia2/esplayer-api.h"

using namespace std;

#define LOG_PER_FEED  500

//#define VIDEO_FEEDING_INTERVAL 300000000L // 300 ms
#define VIDEO_FEEDING_INTERVAL 1 // 1 sec
#define AUDIO_FEEDING_INTERVAL 30000000L // 30 ms

#define BREAK_IF(cond, message, result)                        \
    if(cond) {                                               \
        printf("%s: %s, result:%d\n", __FUNCTION__, message, result);  \
        break; }

int no_video = 0;
int no_audio = 0;
int no_subtitle = 0;
bool quit_all = false;
int repeat_forever = 0;
int forced_video_codec = -1;
int forced_audio_codec = -1;
char** input_files = nullptr;
int input_file_count = 0;
int64_t duration = 0;

int eos_repeat = 0;
int eos_repeat_count = 0;
int eos_repeat_num = 0;

int print_pts = 0;
int trickmode = 0;

LunaServiceCall* lunaService = nullptr;

struct EsplayerContext {
    NDL_EsplayerHandle esp_handle;
    FrameReader* reader = nullptr;

    /* audio,video thread can access framereader at the same time for feeding es.
     * So we have to prevent race condition of frame queue in framereader */
    pthread_mutex_t reader_lock;

    pthread_t audio_pthread;
    pthread_mutex_t audio_lock;
    pthread_cond_t audio_cond;
    pthread_condattr_t audio_attr;

    pthread_t video_pthread;
    pthread_mutex_t video_lock;
    pthread_cond_t video_cond;
    pthread_condattr_t video_attr;

    bool can_feed_audio = false;
    bool can_feed_video = false;
    bool quit = false;
    bool reset = false;
    bool restart = false;   //EOS->pause->flushing->play
    bool flushing = false;

    int64_t play_until = 0; //nanoseconds
    int audio_feed_count = 0;
    int video_feed_count = 0;

    void* audio_feeder();
    void* video_feeder();

    int feed_data(NDL_ESP_STREAM_T type);
    void init_framereader(const char* input_file);
    void esplayer_callback(NDL_ESP_EVENT event, void* playerdata);
    void set_audio_feedable(bool flag, bool signal);
    void set_video_feedable(bool flag, bool signal);

    void run(const char* input_file);
};

inline int64_t current_time_ns() {
    timespec t;
    if(clock_gettime(CLOCK_MONOTONIC, &t) == 0) {
        int64_t ns = t.tv_sec * 1000000000LL + t.tv_nsec;
        return ns;
    }
    return -1;
}

static void* audio_feeder(void* arg)
{
    EsplayerContext* ctxt = (EsplayerContext*)arg;
    return ctxt->audio_feeder();
}

static void* video_feeder(void* arg)
{
    EsplayerContext* ctxt = (EsplayerContext*)arg;
    return ctxt->video_feeder();
}

/* write es data got from framereader to esp */
int EsplayerContext::feed_data(NDL_ESP_STREAM_T type)
{
    printf("%s es_type:%d\n", __func__, type);

    int *feeded_count;

    if(!esp_handle) {
        printf("No esp handle\n");
        return -1;
    }

    pthread_mutex_lock(&reader_lock);
    std::shared_ptr<Frame> frame = reader->getFrame(type);
    pthread_mutex_unlock(&reader_lock);

    if( type == NDL_ESP_AUDIO_ES )
        feeded_count = &audio_feed_count;
    else
        feeded_count = &video_feed_count;

    if(!frame) {
        printf("es_type:%d, Fail to get frame. %d feeded\n", type, *feeded_count);
        return -1;
    }

    printf("%s es_type:%d, pts:%lld\n", __func__, type, frame.get()->timestamp);

    int result = NDL_EsplayerFeedData(esp_handle, frame);

    if(result >= 0) { // result==0 => frame->flags == NDL_ESP_FLAG_END_OF_STREAM
        //printf("feed data : type ; %d size : %d\n", type, result);
        frame->offset += result;
        if (frame->data_len == frame->offset) {
            (*feeded_count)++;
            if(*feeded_count % LOG_PER_FEED == 0) {
                printf("es_type:%d, %d frames are feeded\n", type, *feeded_count);
            }
            pthread_mutex_lock(&reader_lock);
            reader->popFrame(type);
            pthread_mutex_unlock(&reader_lock);

        }
        return result;
    }

    return -1;
}

/* audio feeding thread handler */
void* EsplayerContext::audio_feeder()
{
    int wait_ret = -1;
    int audio_written_size = 0;
    int burst_feed_cnt = 0;
    timespec ts;

    if (!reader->contains(NDL_ESP_AUDIO_ES))
        audio_written_size = -1;
    printf("Start audio feeder thread\n");

    while(!quit)
    {
        pthread_mutex_lock(&audio_lock);
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        {
            printf( "system : clock_gettime error...... \n");
            break;
        }
        ts.tv_nsec += AUDIO_FEEDING_INTERVAL;
        //ts.tv_sec += 1;

        printf( "---Audio feeder wait condition during %ldns\n", AUDIO_FEEDING_INTERVAL);
        wait_ret = pthread_cond_timedwait(&audio_cond, &audio_lock, &ts);
        printf( "+++Audio feeder start feeding ret:%d\n", wait_ret);
        pthread_mutex_unlock(&audio_lock);

        if (can_feed_audio && !flushing) {
            do {
                if (audio_written_size >= 0 && reader->contains(NDL_ESP_AUDIO_ES)){
                    audio_written_size = feed_data(NDL_ESP_AUDIO_ES);
                    burst_feed_cnt++;
                }
                usleep (20000);
            } while (!quit && can_feed_audio && (audio_written_size >= 0) /*&& burst_feed_cnt < 60*/);

            if (reader->contains(NDL_ESP_AUDIO_ES))
                audio_written_size =0;

            printf( "audio_feeder %d feeded at once\n", burst_feed_cnt);
            burst_feed_cnt = 0;
        }

        if (play_until && (play_until < current_time_ns())) {
            printf( "%s quit by duration\n", __func__);
            quit = true;
        }
    }
    pthread_exit((void*)0);
}

/* audio feeding thread handler */
void* EsplayerContext::video_feeder()
{
    int wait_ret = -1;
    int video_written_size = 0;
    int burst_feed_cnt = 0;
    timespec ts;

    if (!reader->contains(NDL_ESP_VIDEO_ES))
        video_written_size = -1;

    printf("Start video feeder thread\n");

    while(!quit)
    {
        pthread_mutex_lock(&video_lock);
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        {
            printf( "system : clock_gettime error...... \n");
            break;
        }
        ts.tv_sec += VIDEO_FEEDING_INTERVAL;

        printf( "---Video feeder wait condition during %ds\n", VIDEO_FEEDING_INTERVAL);
        wait_ret = pthread_cond_timedwait(&video_cond, &video_lock, &ts);
        printf( "+++Video feeder start feeding wait_ret:%d\n", wait_ret);
        pthread_mutex_unlock(&video_lock);

        if (can_feed_video && !flushing) {
            do {
                if (video_written_size >= 0 && reader->contains(NDL_ESP_VIDEO_ES)) {
                    video_written_size = feed_data(NDL_ESP_VIDEO_ES);
                    burst_feed_cnt++;
                }
                usleep (10000);
            } while ( !quit && can_feed_video && (video_written_size >= 0) /*&& burst_feed_cnt < 30*/);

            if (reader->contains(NDL_ESP_VIDEO_ES))
                video_written_size = 0;

            printf( "video_feeder %d feeded at once\n", burst_feed_cnt);
            burst_feed_cnt = 0;
        }

        if (play_until && (play_until < current_time_ns())) {
            printf( "%s quit by duration\n", __func__);
            quit = true;
        }
    }
    pthread_exit((void*)0);
}

void usage()
{
    printf("usage: test_esplayer [options...] input-file-name-or-dump-directory-name\n");
    printf("options:\n"
            " --help            : show this message\n"
            " --no-video        : do not use video stream\n"
            " --no-audio        : do not use audio stream\n"
            " --no-subtitle     : do not enabled subtitle\n"
            " --audio-codec num : set audio codec to num\n"
            " --video-codec num : set video codec to num\n"
            " --repeat          : repeat forever\n"
            " --duration num    : stop after num seconds\n"
            " --eos-repeat      : repeat play without unload after eos forever \n"
            " --eos-repeat-cnt num : stop after num repeat playing with -eos-repeat \n"
            " --trick-mode      : enable trick mode for I-frame video only decoding \n"
            " --print-pts       : enable print pts when it get render done event \n"
          );
}

void EsplayerContext::init_framereader(const char* input_file)
{
    struct stat sb;
    if(stat(input_file, &sb) == -1) {
        perror("stat");
        exit(-1);
    }

    if(S_ISREG(sb.st_mode)) {
        printf("using input file: %s\n", input_file);
        reader = new FfmpegReader(no_video?false:true, no_audio?false:true);
    } else if(S_ISDIR(sb.st_mode)) {
        printf("load ES dump files from : %s\n", input_file);
        reader = new EsDumpReader(no_video?false:true, no_audio?false:true);
    } else {
        usage();
        exit(-1);
    }

    if(!reader->init(input_file)) {
        printf("error in framefeeder.init()\n");
        exit(-1);
    }

    if(reader->contains(NDL_ESP_VIDEO_ES) && (forced_video_codec>0)) {
        printf("force video codec to %d instead of %d\n",
                forced_video_codec, reader->getVideoCodec());
        reader->setForcedVideoCodec((NDL_ESP_VIDEO_CODEC)forced_video_codec);
    }
    if(reader->contains(NDL_ESP_AUDIO_ES) && (forced_audio_codec>0)) {
        printf("force audio codec to %d instead of %d\n",
                forced_audio_codec, reader->getAudioCodec());
        reader->setForcedAudioCodec((NDL_ESP_AUDIO_CODEC)forced_audio_codec);
    }
}

void esplayer_callback(NDL_ESP_EVENT event, void* playerdata, void* userdata)
{
    EsplayerContext* ctxt = (EsplayerContext*) userdata;
    ctxt->esplayer_callback(event, playerdata);
}

void EsplayerContext::set_audio_feedable(bool flag, bool signal)
{
    if( can_feed_audio == flag ){
        printf("can_feed_audio already %d\n", flag);
        return;
    }

    pthread_mutex_lock(&audio_lock);
    can_feed_audio = flag;
    if( flag )
        printf("    start feeding (audio)\n");
    else
        printf("    stop feeding (audio)\n");

    if( signal ) {
        printf("    send signal flag changed(audio)\n");
        pthread_cond_signal(&audio_cond);
    }
    pthread_mutex_unlock(&audio_lock);
}

void EsplayerContext::set_video_feedable(bool flag, bool signal)
{
    if( can_feed_video == flag ){
        printf("can_feed_video already %d\n", flag);
        return;
    }

    pthread_mutex_lock(&video_lock);
    can_feed_video = flag;
    if( flag )
        printf("    start feeding (video)\n");
    else
        printf("    stop feeding (video)\n");

    if( signal ) {
        printf("    send signal flag changed(video)\n");
        pthread_cond_signal(&video_cond);
    }
    pthread_mutex_unlock(&video_lock);
}

void EsplayerContext::esplayer_callback(NDL_ESP_EVENT event, void* playerdata)
{
    switch(event) {
        case NDL_ESP_FIRST_FRAME_PRESENTED:
            {
                printf("\nNDL_ESP_FIRST_FRAME_PRESENTED\n");
                break;
            }
        case NDL_ESP_AUDIO_PORT_CHANGED:
            {
                printf("\nAUDIO PORT CHANGED\n");
                set_audio_feedable(true, true);
                break;
            }
        case NDL_ESP_VIDEO_PORT_CHANGED:
            {
                printf("VIDEO PORT CHANGED\n");
                set_video_feedable(true, true);
                break;
            }

        case NDL_ESP_VIDEOCONFIG_DECODED:
            {
                printf("%s NDL_ESP_VIDEOCONFIG_DECODED\n", __FUNCTION__);
                break;
            }
        case NDL_ESP_AUDIOCONFIG_DECODED:
            {
                printf("%s NDL_ESP_AUDIOCONFIG_DECODED\n", __FUNCTION__);
                break;
            }
        case NDL_ESP_LOW_THRESHOLD_CROSSED_VIDEO:
            printf("LOW THRESHOLD VIDEO\n");
            set_video_feedable(true, true);
            break;

        case NDL_ESP_HIGH_THRESHOLD_CROSSED_VIDEO:
            printf("HIGH THRESHOLD VIDEO\n");
            set_video_feedable(false, true);
            break;

        case NDL_ESP_LOW_THRESHOLD_CROSSED_AUDIO:
            printf("LOW THRESHOLD AUDIO\n");
            set_audio_feedable(true, true);
            break;

        case NDL_ESP_HIGH_THRESHOLD_CROSSED_AUDIO:
            printf("HIGH THRESHOLD AUDIO\n");
            set_audio_feedable(false, false);
            break;

        case NDL_ESP_STREAM_DRAINED_VIDEO:
            {
                printf("stream drained !!!\n");
                break;
            }
        case NDL_ESP_STREAM_DRAINED_AUDIO:
            {
                printf("stream drained audio !!!\n");
                break;
            }
        case NDL_ESP_END_OF_STREAM:
            {
                printf("end of stream !!!\n");
                eos_repeat_num++;
                //restart play without unload if --eos-repeat is set
                //    and restart until eos_repeat_num reach eos_repeat_count if eos_repeat_cnt is set
                if(eos_repeat && (!eos_repeat_count || (eos_repeat_num<eos_repeat_count)))
                    restart = 1;
                else
                    quit =1;
                pthread_cond_signal(&audio_cond);
                pthread_cond_signal(&video_cond);
                break;
            }
        case NDL_ESP_RESOURCE_RELEASED_BY_POLICY:
            {
                printf("%s:%d, receive policy action event.\n", __func__, __LINE__);
                break;
            }
        case NDL_ESP_VIDEO_INFO:
            {
                NDL_ESP_VIDEO_INFO_T* data = (NDL_ESP_VIDEO_INFO_T*)playerdata;
                printf("[Test] Receive video resolution. width : %d, height : %d\n", data->width, data->height);
                if (!no_subtitle && lunaService)
                {
                    char connectionId[CONNECTION_ID_BUFFER_SIZE] = "";
                    NDL_EsplayerGetConnectionId(esp_handle, connectionId, sizeof(connectionId));
                    lunaService->enableSubtitle(connectionId);
                }
                break;
            }
    }
}


static int opt_notty;
static struct termios to_org;

static void ui_exit (void)
{
    printf ("restore tty setting at exit\n");
    tcsetattr (STDIN_FILENO, TCSANOW, &to_org);
}

typedef void (*sig_handler_t)(int);
static sig_handler_t sig_org[32];

static void ui_signal (int sig)
{
    void *array[10];
    size_t size;
    printf ("signal %d. org %p. restore tty\n",
            sig, sig_org[sig]);
    tcsetattr (STDIN_FILENO, TCSANOW, &to_org);

    // get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    exit (128+sig);
}

static int ui_init (void)
{
    struct termios to;

    if (opt_notty || !isatty (STDIN_FILENO))
        return 0;

    tcgetattr (STDIN_FILENO, &to);
    to_org = to;

    atexit (ui_exit);
    sig_org[SIGHUP ] = signal (SIGHUP , ui_signal);
    sig_org[SIGINT ] = signal (SIGINT , ui_signal);
    sig_org[SIGABRT] = signal (SIGABRT, ui_signal);
    sig_org[SIGFPE ] = signal (SIGFPE , ui_signal);
    sig_org[SIGKILL] = signal (SIGKILL, ui_signal);
    sig_org[SIGSEGV] = signal (SIGSEGV, ui_signal);

    to.c_lflag &= ~(ICANON | ECHO | ECHOE);
    to.c_cc[VMIN] = 1;
    tcsetattr (STDIN_FILENO, TCSANOW, &to);

    return 0;
}

static int ui_get_input ()
{
    struct pollfd fds[1];
    int ret;
    char buf[1];

    if (opt_notty || !isatty (fileno(stdin)))
    {
        sleep (5);
        return 0;
    }

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    ret = poll (fds, 1, 1000);
    if (ret == 0)
        return 0;

    ret = read (STDIN_FILENO, buf, 1);
    if (ret != 1)
        return 0;
    return buf[0];
}

void parse_options(int argc, char* argv[])
{
    while (1)
    {
        int long_option_index;
        static struct option long_options[] = {
            {"help",        no_argument,        nullptr,    'h'},
            {"no-video",    no_argument,        &no_video,  1 },
            {"no-audio",    no_argument,        &no_audio,  1 },
            {"no-subtitle", no_argument,        &no_subtitle,  1 },
            {"repeat",      no_argument,        &repeat_forever,    1 },
            {"eos-repeat",  no_argument,        &eos_repeat, 1 },
            {"audio-codec", required_argument,  nullptr,    'a'},
            {"video-codec", required_argument,  nullptr,    'v'},
            {"duration",    required_argument,  nullptr,    'd'},
            {"eos-repeat-cnt", required_argument, nullptr,  'c'},
            {"trick-mode",  no_argument,        nullptr, 't' },
            {"print-pts",   no_argument,        nullptr, 'p' },
            {0, 0, 0, 0 }
        };
        int opt = getopt_long (argc, argv, "",
                long_options, &long_option_index);
        if (opt == -1)
            break;
        switch(opt) {
            case 'h': usage(); exit(-1);
            case 'a': forced_audio_codec = atoi(optarg); break;
            case 'v': forced_video_codec = atoi(optarg); break;
            case 'd': duration = (int64_t)atoi(optarg) * 1000000000LL; break;
            case 'c': eos_repeat_count = (int)atoi(optarg); break;
            case 't': trickmode = 1; no_audio = 1; break;
        }
        printf("opt=%d, optarg=%s\n", opt, optarg);
    }

    printf("no_audio=%d, no_video=%d, no_subtitle=%d\n", no_audio, no_video, no_subtitle);
    printf("forced_audio_codec=%d, forced_video_codec=%d\n", forced_audio_codec, forced_video_codec);
    printf("duration=%lld nano-seconds\n", duration);
    printf("eos_repeat_count : %d\n", eos_repeat_count);

    if (optind >= argc) {
        printf("no input files..??? try --help\n");
        exit(-1);
    }

    input_files = &argv[optind];
    input_file_count = argc - optind;

    printf("input file count = %d\n", input_file_count);
    for (int i=0; i<input_file_count; ++i)
        printf("input_files[%d] : %s\n", i, input_files[i]);

    if(forced_audio_codec == 0) {
        no_audio = 1;
    }
    if(forced_video_codec == 0) {
        no_video = 1;
    }

    if(no_video && no_audio) {
        printf("what do you want..???\n");
        exit(-1);
    }

    if(!no_subtitle) {
        lunaService = new LunaServiceCall;
    }
}

int main(int argc, char* argv[])
{
    ui_init();
    parse_options(argc, argv);

    int play_count = 0;

    while(!quit_all) {
        EsplayerContext* esp = new EsplayerContext;
        esp->run(input_files[play_count % input_file_count]);
        bool reset = esp->reset;
        delete esp;

        ++play_count;

        if (reset || repeat_forever || (play_count % input_file_count)) {
            printf("###########################################################\n");
            printf("#####     repeat %d            ############################\n", play_count);
            printf("###########################################################\n");
            continue;
        }

        if (play_count % input_file_count == 0)
            break;
    }

    if (play_count > 0) {
        printf("###########################################################\n");
        printf("#####     repeat count : %d      ##########################\n", play_count);
        printf("###########################################################\n");
    }

    if(lunaService) {
        delete lunaService;
        lunaService = nullptr;
    }
    return 0;
}

void EsplayerContext::run(const char* input_file)
{
    int result = 0;
    std::array<int,5> playback_rate_table {100, 250, 500, 1000, 2000};
    size_t playback_rate = 3; // default playback rate 1000
    char connectionId[CONNECTION_ID_BUFFER_SIZE] = "";
    quit = 0;
    bool videomute = false;
    bool audiomute = false;
    bool audioFade = false;

    init_framereader(input_file);

    pthread_mutex_init(&reader_lock, NULL);

    pthread_mutex_init(&audio_lock, NULL);
    pthread_condattr_init(&audio_attr);
    pthread_condattr_setclock(&audio_attr , CLOCK_MONOTONIC);
    pthread_cond_init(&audio_cond, &audio_attr);

    pthread_mutex_init(&video_lock, NULL);
    pthread_condattr_init(&video_attr);
    pthread_condattr_setclock(&video_attr , CLOCK_MONOTONIC);
    pthread_cond_init(&video_cond, &video_attr);

    NDL_ESP_META_DATA metadata;
    metadata.audio_codec = reader->getAudioCodec();
    metadata.video_codec = reader->getVideoCodec();

    bool has_video = false;
    bool has_audio = false;

    if ((metadata.video_codec != NDL_ESP_VIDEO_NONE) || no_audio) {
        has_video = true;
        pthread_create(&video_pthread, NULL, ::video_feeder, this);
    }

    if ((metadata.audio_codec != NDL_ESP_AUDIO_NONE) || no_video) {
        has_audio = true;
        pthread_create(&audio_pthread, NULL, ::audio_feeder, this);
    }

    if (has_video) {
        video_stream_config *video_config = reader->getVideoStreamConfig();
        if (video_config != NULL) {
            metadata.video_encoding = video_config->video_encoding;
            metadata.width = video_config->width;
            metadata.height = video_config->height;
            //metadata.framerate = video_config->framerate;
            metadata.framerate = 30;
            metadata.extradata = video_config->extradata;
            metadata.extrasize = video_config->extrasize;
        }

    }

    if (has_audio) {
        audio_stream_config *audio_config = reader->getAudioStreamConfig();
        if (audio_config != NULL) {
            metadata.channels      = audio_config->channels;
            metadata.samplerate    = audio_config->samplerate;
            metadata.blockalign    = audio_config->blockalign;
            metadata.bitrate       = 16;//audio_config->bitrate;
            metadata.bitspersample = audio_config->bitspersample;
        }
    }

    do {

        esp_handle = NDL_EsplayerCreate("com.omx.app", ::esplayer_callback, this);
        BREAK_IF(!esp_handle, "error in NDL_EsplayerCreate", 0);

        result = NDL_EsplayerLoad(esp_handle, &metadata);
        BREAK_IF(result != 0, "error in NDL_EsplayerLoad", result);

PLAY:
        result = NDL_EsplayerSetTrickMode(esp_handle, trickmode);
        BREAK_IF(result!=0, "error in NDL_EsplayerSetTrickMode", result);

        result = NDL_EsplayerPlay(esp_handle);
        BREAK_IF(result!=0, "error in NDL_EsplayerPlay", result);

        result = NDL_EsplayerSetPlaybackRate(esp_handle, playback_rate_table[playback_rate]);
        BREAK_IF(result!=0, "error in NDL_EsplayerPlaybackrate", result);

        if(reader->contains(NDL_ESP_VIDEO_ES)){
            printf("first can feed video\n");
            set_video_feedable(true, true);
        }
        if(reader->contains(NDL_ESP_AUDIO_ES)) {
            printf("first can feed audio\n");
            set_audio_feedable(true, true);
        }

        if (duration)
            play_until = current_time_ns() + duration;

        printf ("\nenter input [q:quit, p:play/pause] : \n");

        int paused = 0;
        int ui_input=0;

        while(!quit) {
            //restart play without unload if eos-repeat is set
            if(restart)
            {
                printf("NDL_EsplayerPause\n");
                NDL_EsplayerPause(esp_handle);

                int64_t seekTime=0;
                flushing = true;
                seekTime = 0;
                printf("SeekTo : (%lld) \n\n", seekTime);
                reader->seekTo(seekTime);
                restart = false;

                NDL_EsplayerSetTrickMode(esp_handle, 0);

                printf("NDL_EsplayerFlush\n");
                NDL_EsplayerFlush(esp_handle);
                flushing = false;

                // feed data after flushing
                if(reader->contains(NDL_ESP_VIDEO_ES) ||
                        reader->contains(NDL_ESP_AUDIO_ES) ) {
                    printf("seek -> flush -> need more data\n");
                    set_audio_feedable(true, true);
                    set_video_feedable(true, true);
                }
                goto PLAY;
            }

            ui_input = ui_get_input();
            switch (ui_input)
            {
                case 'c':
                    printf("eos-repeated >>> %d \n\n", eos_repeat_num);
                    break;
                case 'q':
                    quit = true;
                    quit_all = true;
                    repeat_forever = false;
                    pthread_cond_signal(&audio_cond);
                    pthread_cond_signal(&video_cond);
                    break;
                case 'r'://reset
                    quit = true;
                    reset = true; // if reset, it'll continue to play next input file
                    pthread_cond_signal(&audio_cond);
                    pthread_cond_signal(&video_cond);
                    break;
                case ' ':
                    break;
                    //'d' : seek backward 5sec.
                    //'f' : flush current position
                    //'g' : seek forward 5sec.
                case 'd':
                case 'f':
                case 'g':
                    printf("esplayer-test Flush +\n");
                    flushing = true;
                    {
                        int64_t startTime=0;
                        int64_t currTime=0;
                        int64_t seekTime=0;
                        if (NDL_EsplayerGetMediatime(esp_handle, &startTime, &currTime)==0)
                        {
                            if (ui_input == 'd')
                                seekTime = (startTime+currTime-5000000)*AV_TIME_BASE/1000000;
                            else if (ui_input == 'g')
                                seekTime = (startTime+currTime+5000000)*AV_TIME_BASE/1000000;
                            else
                                seekTime = (startTime+currTime)*AV_TIME_BASE/1000000;

                            if (seekTime > reader->getDuration()) {
                                printf("seek time exceeds duration. seekTime : %lld, duration : %lld\n",
                                        seekTime, reader->getDuration());
                                quit = 1;
                                pthread_cond_signal(&audio_cond);
                                pthread_cond_signal(&video_cond);
                                break;
                            }

                            if (seekTime < 0) seekTime = 0;
                            printf("SeekTo : (%lld) %lld, %lld \n\n", seekTime, startTime, currTime);
                            reader->seekTo(seekTime);
                        }
                    }
                    printf("NDL_EsplayerFlush \n");
                    NDL_EsplayerFlush(esp_handle);
                    flushing = false;
                    // feed data after flushing
                    if(reader->contains(NDL_ESP_VIDEO_ES) ||
                            reader->contains(NDL_ESP_AUDIO_ES) ) {
                        printf("seek -> flush -> need more data\n");
                        set_audio_feedable(true, true);
                        set_video_feedable(true, true);
                    }

                    printf("esplayer-test Flush -\n");
                    break;
                    //'s' : step a frame
                case 's':
                    if(!paused) {
                        printf("player is not paused !!\n");
                        break;
                    }
                    printf("NDL_EsplayerStepFrame +\n");
                    NDL_EsplayerStepFrame(esp_handle);
                    printf("NDL_EsplayerStepFrame -\n");
                    break;
                    //'>' : increase speed 0.1 up to 2x
                case '>':
                    if (playback_rate >= (playback_rate_table.size()-1))
                    {
                        printf("current playback rate : %d (0x%x). it is already max.\n",
                                playback_rate_table[playback_rate],
                                playback_rate_table[playback_rate]);
                        break;
                    }
                    playback_rate += 1;
                    NDL_EsplayerSetPlaybackRate(esp_handle, playback_rate_table[playback_rate]);
                    break;
                    //'<' : decrease speed 0.1 up to 0
                case '<':
                    if (playback_rate == 0)
                    {
                        printf("current playback rate : %d (0x%x). it is already min.\n",
                                playback_rate_table[playback_rate],
                                playback_rate_table[playback_rate]);
                        break;
                    }
                    playback_rate -= 1;
                    NDL_EsplayerSetPlaybackRate(esp_handle, playback_rate_table[playback_rate]);
                    break;
                    //'p' : toggle pause/resume
                case 'p':
                    paused = paused ? 0 : 1;
                    if(paused) {
                        printf("NDL_EsplayerPause\n");
                        NDL_EsplayerPause(esp_handle);
                    } else {
                        printf("NDL_EsplayerPlay\n");
                        NDL_EsplayerPlay(esp_handle);
                    }
                    break;
                case 'a':
                    audiomute = !audiomute;
                    NDL_EsplayerMuteAudio(esp_handle, audiomute);
                    printf("audio mute toggle. %c\n", audiomute ? '+' : '-');
                    break;
                case 'v':
                    videomute = !videomute;
                    NDL_EsplayerMuteVideo(esp_handle, videomute);
                    printf("video mute toggle. %c\n", videomute ? '+' : '-');
                    break;
                    //'t' : setTrickMode
                case 't':
                    printf("NDL_EsplayerSetTrickMode\n");
                    trickmode = !trickmode;
                    NDL_EsplayerSetTrickMode(esp_handle, trickmode);
                    break;
                    //'e' : easeVolume
                case 'e':
                    printf("NDL_EsplayerSetVolume\n");
                    audioFade = !audioFade;
                    if(audioFade) {
                        /* volume = 20, ease duration = 2000msec & type = linear */
                        NDL_EsplayerSetVolume(esp_handle, 20, 2000, EASE_TYPE_LINEAR);
                    }
                    else {
                        /* volume = 80, ease duration = 5000msec & type = OutCubic */
                        NDL_EsplayerSetVolume(esp_handle, 80, 5000, EASE_TYPE_OUTCUBIC);
                    }
                    break;
                default:
                    break;
            }
        }

        pthread_join(audio_pthread,  NULL);
        pthread_join(video_pthread,  NULL);
        pthread_cond_destroy(&audio_cond);
        pthread_cond_destroy(&video_cond);
        pthread_mutex_destroy(&video_lock);
        pthread_mutex_destroy(&audio_lock);
        pthread_mutex_destroy(&reader_lock);

        result = NDL_EsplayerUnload(esp_handle);
        BREAK_IF(result!=0, "error in NDL_EsplayerUnload", result);

        NDL_EsplayerDestroy(esp_handle);
        esp_handle = 0;

        printf("############# Exit ###############\n");
    } while(0);

    if( reader ) {
        delete reader;
        reader = nullptr;
    }
}
