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

#ifndef NDL_DIRECTMEDIA2_ESPLAYER_H_
#define NDL_DIRECTMEDIA2_ESPLAYER_H_

#include <stddef.h>
#include <atomic>
#include <queue>
#include <functional>
#include <algorithm>
#include <math.h>
#include "mediaresource/requestor.h"
#include "ndl-directmedia2/states.h"
#include "message.h"
#include "component.h"
#include "clock.h"

// for audio sw decoder
#include "audioswdecoder.h"

//#define VIDEO_THRESHOLD_CONTROL //TODO: under construction

typedef unsigned int    DWORD;
typedef unsigned short  WORD;
typedef unsigned char   BYTE;
typedef struct tGUID
{
    DWORD Data1;
    WORD  Data2, Data3;
    BYTE  Data4[8];
} __attribute__((__packed__)) GUID;

typedef struct tWAVEFORMATEX
{
    WORD    wFormatTag;
    WORD    nChannels;
    DWORD   nSamplesPerSec;
    DWORD   nAvgBytesPerSec;
    WORD    nBlockAlign;
    WORD    wBitsPerSample;
    WORD    cbSize;
} __attribute__((__packed__)) WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;

typedef struct tWAVEFORMATEXTENSIBLE
{
    WAVEFORMATEX Format;
    union
    {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        WORD wReserved;
    } Samples;
    DWORD dwChannelMask;
    GUID SubFormat;
} __attribute__((__packed__)) WAVEFORMATEXTENSIBLE;


typedef std::function<void(NDL_ESP_EVENT event, void* playerdata, void* userdata)> NDL_EsplayerCallback;
typedef std::shared_ptr<NDL_ESP_STREAM_BUFFER> NDL_EsplayerBuffer;

namespace NDL_Esplayer {

    class ResourceRequestor;
    class Clock;

    class Esplayer {
        public:
            Esplayer(std::string app_id, NDL_EsplayerCallback callback, void* userdata);
            virtual ~Esplayer();

            int load(NDL_ESP_META_DATA* meta);
            int loadEx(NDL_ESP_META_DATA* meta, NDL_ESP_PTS_UNITS units);
            int unload();
            int getConnectionId(char* buf, size_t buf_len) const;
            const std::string& getConnectionId() const;
            int reloadAudio(NDL_ESP_META_DATA* meta);

            int feedData(NDL_EsplayerBuffer buff);
            int flush();
            int getBufferLevel(NDL_ESP_STREAM_T type,
                    uint32_t* level);

            int play();
            int pause();
            int stepFrame();
            int setPlaybackRate(int rate);
            int setTrickMode(bool enable);
            int setVolume(int volume, int duration = 0, NDL_ESP_EASE_TYPE type = EASE_TYPE_LINEAR);
            int getMediaTime(int64_t* start_time, int64_t* current_time);

            int notifyForegroundState(const NDL_ESP_APP_STATE appState);
            inline NDL_ESP_STATUS getStatus() const { return state_.get(); }

            // Application has higher priority than soc's decision. sometimes ES doesn't have 3D type data.
            int set3DType(const NDL_ESP_3D_TYPE e3DType);

            int setVideoDisplayWindow(const long left, const long top, const long width, const long height,
                    const bool isFullScreen) const;
            int setVideoCustomDisplayWindow(const long src_left, const long src_top,
                    const long src_width, const long src_height,
                    const long dst_left, const long dst_top,
                    const long dst_width, const long dst_height,
                    const bool isFullScreen) const;

            // mute controlled by AV block of TV service
            int muteAudio(bool mute);
            int muteVideo(bool mute);

        private:
            void notifyClient(NDL_ESP_EVENT event, void* playerdata = nullptr);

        private:
            int loadClockComponent();
            int loadVideoComponents(NDL_ESP_VIDEO_CODEC codec);
            int loadAudioComponents(NDL_ESP_AUDIO_CODEC codec);

            int changeComponentsState(OMX_STATETYPE state, int timeout_seconds);
#if 0 //not used
            int waitForComponentsState(OMX_STATETYPE state, int timeout_seconds);
#endif
            int setComponentsFlush(int timeout_seconds);
#if 0 //not used
            int waitForComponentsFlush(int timeout_seconds);
#endif

            void onAudioRenderEvent(int64_t timestamp = 0);
            void onVideoRenderEvent(int64_t timestamp = 0);

            void onVideoInfoEvent(void* data);

        private:
            class State {
                public:
                    State();
                    bool canTransit(NDL_ESP_STATUS status) const;
                    inline void transit(NDL_ESP_STATUS status) {
                        if (status_ != status) {
                            status_ = status;
                            if (state_cb_) { state_cb_(status_); }
                        }
                    }
                    inline NDL_ESP_STATUS get() const { return status_; }
                    void subscribeState(std::function<void(const NDL_ESP_STATUS)> cb) { state_cb_ = cb; };
                private:
                    NDL_ESP_STATUS status_;
                    std::function<void(const NDL_ESP_STATUS)> state_cb_ {nullptr};
            };

            // video codec related stubs
            int onVideoCodecCallback(int event,
                    uint32_t data1,
                    uint32_t data2,
                    void* data);

            // video renderer related stubs
            int onVideoRendererCallback(int event,
                    uint32_t data1,
                    uint32_t data2,
                    void* data);
            int onVideoSchedulerCallback(int event,
                    uint32_t data1,
                    uint32_t data2,
                    void* data);

            // audio codec related stubs
            int onAudioCodecCallback(int event,
                    uint32_t data1,
                    uint32_t data2,
                    void* data);
            int onAudioCodecDetected();

#if SUPPORT_AUDIOMIXER
            // audio mixer related stubs
            int onAudioMixerCallback(int event,
                    uint32_t data1,
                    uint32_t data2,
                    void* data);
#endif
            // audio codec related stubs
            int onAudioRendererCallback(int event,
                    uint32_t data1,
                    uint32_t data2,
                    void* data);

            // stepping related stubs, used in non-tunnel mode only
            int stepFrameNonTunnelMode();
            std::mutex stepping_mutex_;
            std::condition_variable stepping_cond_;
            bool stepping_ {false};

            // buffer re-configuring
            std::mutex reconfiguring_mutex_;
            std::condition_variable reconfiguring_cond_;
            std::atomic<bool> reconfiguring_ {false};

            // slow trick play related stubs, used in non-tunnel mode only
            std::mutex frame_queue_mutex_[2];
            std::atomic<bool> has_rendering_started_{false};

            std::queue<NDL_EsplayerBuffer> stream_buff_queue_[2];

            void startRendering();
            bool isReadyToRender();
            enum {DELAY_ON_VIDEO_ONLY_RENDER = 50*1000*1000};//wait for audio 50ms at most

            void clearFrameQueues();

            void pushBufQueue(NDL_EsplayerBuffer buf);
            void popBufQueue(const NDL_ESP_STREAM_T& stream_type);
            NDL_ESP_STREAM_BUFFER* getBufQueue(const NDL_ESP_STREAM_T& stream_type);
            void clearBufQueue(NDL_ESP_STREAM_T stream_type);

            // to compensate PTS wraparound
            int64_t pts_base_[2] {0,0}; // increase by max on each wraparound
            int64_t pts_previous_[2] {-1,-1};
            int64_t adjustPtsToMicrosecond(int index, int64_t pts);
            uint32_t setOmxFlags(int64_t /*double*/ pts, uint32_t buffer_flags, NDL_ESP_STREAM_T streamType);
            enum {PTS_DROP_THRESHOLD = 10};
            int pts_drop_count_[2] {0,0}; // change pts_base_ and pts_previous_
            //   only if enough number of pts drops are detected

            void sendVideoDecoderConfig();
            void sendAudioDecoderConfig();
            WAVEFORMATEXTENSIBLE m_wave_header;

            std::shared_ptr<AudioSwDecoder> audio_sw_decoder_ {nullptr};

            int Feed_AudioData(void);
            int Feed_VideoData(void);

            void printMetaData(const NDL_ESP_META_DATA* meta) const;
            void printComponentInfo() const;
            void printStreamBuf(const char* prefix_str, NDL_ESP_STREAM_BUFFER* buf);

        public:
            // just hand over callback to State class(inner class)
            void subscribePlayerState(std::function<void(const NDL_ESP_STATUS)> cb) { state_.subscribeState(cb); }

            const NDL_ESP_VIDEO_INFO_T& getVideoInfo() const { return videoInfo_; }

        private:

            bool foreground_ = false;

            enum syncState {
                INVALID = -1,
                ALLOW_VIDEO = 0,
                HOLD_VIDEO,
                SKIP_VIDEO
            };

            State state_;
            syncState sync_state_ {ALLOW_VIDEO};
            std::string appId_ {nullptr};
            NDL_EsplayerCallback callback_ {nullptr};
            void* userdata_ {nullptr};

            bool enable_audio_ {false};
            bool enable_video_ {false};
            bool enable_video_tunnel_ {false};
            bool enable_audio_tunnel_ {false};

            enum {VIDEO_DROP_THRESHOLD = 0}; //ms
            enum {VIDEO_LAG_DELAY_THRESHOLD = 1000000}; //ms
            enum OMX_SETTINGS {

                PORT_CLOCK_AUDIO = 80,
                PORT_CLOCK_VIDEO = 81,
                PORT_CLOCK_AUDIO_MIXER = 82,

                VIDEO_IN_BUFFER_COUNT_LOW = 10,
                AUDIO_IN_BUFFER_COUNT_LOW = 10,

                VIDEO_IN_BUFFER_COUNT_HIGH = 30,

                VIDEO_MSG_COUNT_HIGH = 30,

                VIDEO_IN_BUFFER_MKSEC_SKIP = -100000, //100ms
                VIDEO_IN_BUFFER_MKSEC_LOW = 250000, //250ms
                VIDEO_IN_BUFFER_MKSEC_HIGH = 1000000 //1000ms
            };

            std::shared_ptr<Clock> clock_;

            std::shared_ptr<Component> video_codec_;
            std::shared_ptr<Component> video_renderer_;
            std::shared_ptr<Component> video_scheduler_;

            MessageLooper video_message_looper_;
            MessageLooper video_renderer_looper_;
            bool video_eos_ {false};

            std::shared_ptr<Component> audio_codec_;
            std::shared_ptr<Component> audio_renderer_;
            std::shared_ptr<Component> audio_mixer_;

            MessageLooper audio_message_looper_;
            MessageLooper audio_renderer_looper_;
            bool audio_eos_ {false};

            std::atomic<bool> loaded_ {false};
            std::mutex unload_mutex_;

            std::shared_ptr<ResourceRequestor> rm_;
            std::string connectionId_;

            NDL_ESP_VIDEO_INFO_T videoInfo_ {};
            NDL_ESP_3D_TYPE appForced3Dtype_ {E3DTYPE_NONE};

            // notify client on the first video render event
            //   if playing audio only, notify on the first audio render event
            bool waiting_first_frame_presented_ {true};
            bool low_threshold_crossed_audio_ {true};

            // renderer should be executed when first port setting change occurs
            bool is_first_port_setting_change_ {true};

            int target_playback_rate_ {Clock::NORMAL_PLAYBACK_RATE};
            unsigned int frame_count_ {0};
            bool trick_mode_enabled_ {false};
            bool is_video_dropped_ {false};

            int64_t audio_last_pts_ {0};
            int64_t video_last_pts_ {0};

            inline bool isInterlacedVideo() { return videoInfo_.SCANTYPE == SCANTYPE_INTERLACED; }
            int video_lagging_count_ {0};
            int64_t last_video_lag_timestamp_ {0};

            NDL_ESP_META_DATA meta_{NDL_ESP_VIDEO_NONE,
                NDL_ESP_AUDIO_NONE};

            NDL_ESP_PTS_UNITS pts_units_{NDL_ESP_PTS_TICKS};

            Esplayer(Esplayer const&) = delete;
            void operator=(Esplayer const&) = delete;


            bool sendVideoExtradata {false};
            int32_t plane_id_;


    };

} //namespace NDL_Esplayer

#endif //#ifndef NDL_DIRECTMEDIA2_ESPLAYER_H_
