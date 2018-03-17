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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "esplayer.h"

//TODO: decide how to handle vendor specific header files
#include <OMX_Types.h>

#include <OMX_Broadcom.h>

#include "omxclock.h"
#include "component.h"
#include "omx/omxclient.h"

//Just for debugging
struct timeval audioPrevious, videoPrevious;

using namespace NDL_Esplayer;

#define LOGTAG "esplayer"
#include "debug.h"

#define LOG_FEEDING             NDL_LOGV
#define LOG_FEEDINGV            NDL_LOGV
#define LOG_INOUT               NDL_LOGV
#define LOG_INPUTBUF            NDL_LOGV

#define SPEAKER_FRONT_LEFT            0x00001
#define SPEAKER_FRONT_LEFT_OF_CENTER  0x00040
#define SPEAKER_FRONT_RIGHT           0x00002
#define SPEAKER_FRONT_RIGHT_OF_CENTER 0x00080
#define WAVE_FORMAT_PCM               0x0001

#define MAX_PORT_WAIT_TIME  1 //1 sec
#define MAX_STATE_WAIT_TIME  1 //1 sec
#define MAX_FLUSH_WAIT_TIME  3 //3 sec
#if SUPPORT_ALSA_RENDERER_COMPONENT
#define AUDIO_OUTPUT_DESTINATION    "hw:0,0"
#else
#define AUDIO_OUTPUT_DESTINATION    "hdmi"
#endif
static const GUID KSDATAFORMAT_SUBTYPE_PCM = {
    WAVE_FORMAT_PCM,
    0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

namespace {
    inline uint32_t translateToOmxFlags(uint32_t flags)
    {
        uint32_t omxflags = 0;
        if (flags & NDL_ESP_FLAG_END_OF_STREAM)
            omxflags |= OMX_BUFFERFLAG_EOS;
        return omxflags;
    }
    inline uint32_t translateToNDLFlags(uint32_t flags)
    {
        uint32_t ndlflags = 0;
        if (flags & OMX_BUFFERFLAG_EOS)
            ndlflags |= NDL_ESP_FLAG_END_OF_STREAM;
        return ndlflags;
    }
}

Esplayer::Esplayer(std::string app_id, NDL_EsplayerCallback callback, void* userdata)
    : appId_(app_id), callback_(callback), userdata_(userdata), plane_id_(0)
{
    rm_ = std::make_shared<ResourceRequestor>(appId_);
    connectionId_ = rm_->getConnectionId(); // must have creation time.
    video_message_looper_.setName("VCodecLooper");
    video_renderer_looper_.setName("VRenderLooper");
    audio_message_looper_.setName("ACodecLooper");
    audio_renderer_looper_.setName("ARenderLooper");
}

Esplayer::~Esplayer()
{
    NDLLOG(LOGTAG, NDL_LOGI, "Esplayer Destroy");
    if (video_renderer_ || audio_renderer_) {
        flush();
        unload();
    }
    clearBufQueue(NDL_ESP_VIDEO_ES);
    clearBufQueue(NDL_ESP_AUDIO_ES);
    OmxClient::destroyCoreInstance();
}

Esplayer::State::State()
    :status_(NDL_ESP_STATUS_IDLE)
{
}

bool Esplayer::State::canTransit(NDL_ESP_STATUS status) const
{
    const bool o = true;
    const bool x = false;
    const bool statusmap[NDL_ESP_STATUS_COUNT][NDL_ESP_STATUS_COUNT] = {

        /*from    to: IDLE  LOAD  PLAY  PAUSE UNLOAD FLUSH STEP  EOS  */
        /*IDLE    */  {o,    o,    x,    x,    x,    o,    x,    x},
        /*LOADED  */  {x,    x,    o,    o,    o,    o,    x,    x},
        /*PLAYING */  {x,    x,    o,    o,    o,    o,    x,    x},
        /*PAUSED  */  {x,    x,    o,    o,    o,    o,    o,    x},
        /*UNLOADED*/  {x,    x,    x,    x,    o,    o,    x,    x},
        /*FLUSHING*/  {x,    x,    o,    o,    o,    o,    x,    x},
        /*STEPPING*/  {x,    x,    o,    o,    o,    x,    x,    x},
        /*EOS     */  {x,    x,    x,    x,    o,    o,    x,    x},
    };
    return statusmap[status_][status];
}

void Esplayer::notifyClient(NDL_ESP_EVENT event, void* playerdata)
{
    if (callback_) {
        NDLLOG(LOGTAG, NDL_LOGV, "callback_ event:%d", event);
        callback_(event, playerdata, userdata_);
    }
}

int Esplayer::load(NDL_ESP_META_DATA* meta)
{
    NDLASSERT(meta);
    NDLASSERT(state_.canTransit(NDL_ESP_STATUS_LOADED));

    if (loaded_) {
        NDLLOG(LOGTAG, NDL_LOGI, "block duplicate load call");
        return NDL_ESP_RESULT_SUCCESS;
    }

    NDLLOG(SDETTAG, LOG_INOUT, "%s +", __func__);

    printMetaData(meta);

    if (!state_.canTransit(NDL_ESP_STATUS_LOADED)) {
        NDLLOG(LOGTAG, NDL_LOGE, "unsupported operation, current state:%d", state_.get());
        return NDL_ESP_RESULT_FAIL;
    }

    loaded_.exchange(true);
    ///////////////////////////////////////////////////////////////////////////
    // resource manager - acquire resource
    //
    PortResource_t resourceMMap;
    //    if (foreground_)
    rm_->notifyForeground();
    rm_->registerUMSPolicyActionCallback( [this] () {
            // system resource released by policy.
            NDLLOG(LOGTAG, NDL_LOGE, "UMSPolicyActionCallback...!!!!!!!");
            notifyClient(NDL_ESP_RESOURCE_RELEASED_BY_POLICY);
            unload();
            });

    rm_->registerPlaneIdCallback( [this] (int32_t planeIdIdx) -> bool {
            NDLLOG(LOGTAG, NDL_LOGI, "Plane Id Got!!!!!!!");
            plane_id_ = planeIdIdx;
            return true;
            });

    NDLLOG(LOGTAG, NDL_LOGD, "acquire resource. vcodec : %d, acodec : %d",
            meta->video_codec, meta->audio_codec);

    if (!rm_->acquireResources(meta, resourceMMap))
    {
        NDLLOG(LOGTAG, NDL_LOGE, "acquire failed!!");
        return NDL_ESP_RESULT_FAIL;
    }

    enable_audio_ = (meta->audio_codec != NDL_ESP_AUDIO_NONE);
    enable_video_ = (meta->video_codec != NDL_ESP_VIDEO_NONE);

    ///////////////////////////////////////////////////////////////////////////
    // SW audio decoder initialize
    //
    if (enable_video_) {
        meta_.width     =  meta->width;
        meta_.height    =  meta->height;
        meta_.framerate =  meta->framerate;
        meta_.extradata =  meta->extradata;
        meta_.extrasize =  meta->extrasize;
    }

    if (enable_audio_) {
        meta_.channels      = 2;//meta->channels;
        meta_.samplerate    = meta->samplerate;
        meta_.blockalign    = meta->blockalign;
        meta_.bitrate       = meta->bitrate;
        meta_.bitspersample = 16;//meta->bitspersample;

        enum AVCodecID codec_id = AV_CODEC_ID_NONE;

        switch (meta->audio_codec) {
            case NDL_ESP_AUDIO_CODEC_MP2:
                codec_id = CODEC_ID_MP2;
                break;
            case NDL_ESP_AUDIO_CODEC_MP3:
                codec_id = CODEC_ID_MP3;
                break;
            case NDL_ESP_AUDIO_CODEC_AC3:
                codec_id = CODEC_ID_AC3;
                break;
            case NDL_ESP_AUDIO_CODEC_EAC3:
                codec_id = CODEC_ID_EAC3;
                break;
            case NDL_ESP_AUDIO_CODEC_AAC:
                codec_id = CODEC_ID_AAC;
                break;
            case NDL_ESP_AUDIO_CODEC_HEAAC:
                codec_id = CODEC_ID_AAC_LATM;
                break;
            case NDL_ESP_AUDIO_CODEC_PCM_44100_2CH:
            case NDL_ESP_AUDIO_CODEC_PCM_48000_2CH:
                codec_id = AV_CODEC_ID_PCM_S16LE;
                break;
            default:
                NDLLOG(LOGTAG, NDL_LOGE, "%s: unsupported audio codec:%d", __func__, meta->audio_codec);
                break;
        }
        NDLLOG(LOGTAG, NDL_LOGD, "metadata audio_codec:%d, channel:%d, samplerate:%d, bitrate:%d, blockalign:%d, bitspersample:%d",
                meta->audio_codec, meta->channels, meta->samplerate, meta->bitrate, meta->blockalign, meta->bitspersample);

        if (codec_id != AV_CODEC_ID_NONE && codec_id != AV_CODEC_ID_PCM_S16LE) {
            audio_sw_decoder_ = std::make_shared<AudioSwDecoder>();
            if (audio_sw_decoder_ != NULL) {
                NDLLOG(LOGTAG, NDL_LOGD, "audio sw decoder is created", __func__);
                audio_sw_decoder_->audio_stream_info_.codec_id              = codec_id;
                audio_sw_decoder_->audio_stream_info_.channels              = meta_.channels;
                audio_sw_decoder_->audio_stream_info_.sample_rate           = meta_.samplerate;
                audio_sw_decoder_->audio_stream_info_.bit_rate              = (int64_t)meta_.bitrate;
                audio_sw_decoder_->audio_stream_info_.block_align           = meta_.blockalign;
                audio_sw_decoder_->audio_stream_info_.bits_per_coded_sample = meta_.bitspersample;
                if( audio_sw_decoder_->OpenAudio(codec_id) == false ){
                    NDLLOG(LOGTAG, NDL_LOGE, "audio sw decoder codec_id:0x%x open error", __func__, codec_id);
                    return NDL_ESP_RESULT_FAIL;
                }
            }
            else {
                NDLLOG(LOGTAG, NDL_LOGE, "audio sw decoder creation error", __func__);
                return NDL_ESP_RESULT_FAIL;
            }
        }
        else if( codec_id == AV_CODEC_ID_NONE ) {
            NDLLOG(LOGTAG, NDL_LOGE, "%s unsupported audio codec", __func__);
            return NDL_ESP_RESULT_FAIL;
        }

    }
    //save codec infomation
    meta_.video_codec = meta->video_codec;
    meta_.audio_codec = meta->audio_codec = NDL_ESP_AUDIO_CODEC_PCM_44100_2CH;

    low_threshold_crossed_audio_ = true;

    audio_eos_ = false;
    video_eos_ = false;

    trick_mode_enabled_ = false;
    is_video_dropped_ = false;
    has_rendering_started_ = false;

    video_lagging_count_ = 0;
    last_video_lag_timestamp_ = 0;

    ///////////////////////////////////////////////////////////////////////////
    // resource port setting - SIC dependent
    //
    int result = NDL_ESP_RESULT_FAIL;

    //TODO : vendor dependent (SIC)
    do {
        ///////////////////////////////////////////////////////////////////////////
        // prepare clock
        //
        result = NDL_ESP_RESULT_CLOCK_ERROR;
        BREAK_IF_NONZERO(loadClockComponent(), "creating clock component");

        result = NDL_ESP_RESULT_CLOCK_STATE_ERROR;
        BREAK_IF_NONZERO(clock_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
                "setting clock to idle state and wait done");

        if (enable_video_) {
            result = NDL_ESP_RESULT_VIDEO_UNSUPPORTED;
            BREAK_IF_NONZERO(loadVideoComponents(meta->video_codec),
                    "creating video components");

            // set video parameters
            result = NDL_ESP_RESULT_VIDEO_CODEC_ERROR;
            BREAK_IF_NONZERO(video_codec_->setVideoFormat(meta),
                    "setting video param");

            // configure port buffers
            BREAK_IF_NONZERO(
                    video_codec_->configureInputBuffers(video_codec_->getInputBufferCount(),
                        video_codec_->getInputBufferSize()),
                    "reconfiguring video input buffer");

            // go to idle state
            result = NDL_ESP_RESULT_VIDEO_STATE_ERROR;
            BREAK_IF_NONZERO(video_codec_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
                    "setting video decoder to idle state");

            // Allocate video codec  input buffer
            result = NDL_ESP_RESULT_VIDEO_BUFFER_ERROR;
            BREAK_IF_NONZERO(video_codec_->allocateInputBuffer(),
                    "allocating video decoder input buffers");
            BREAK_IF_NONZERO(video_codec_->waitForPortEnable(video_codec_->getInputPortIndex(), true, MAX_PORT_WAIT_TIME),
                    "waitForPortEnable for video decoder");

            // setup video tunneling
            // video_codec_ -> video_scheduler_ -> video_renderer
            result = NDL_ESP_RESULT_VIDEO_TUNNEL_ERROR;
            BREAK_IF_NONZERO(clock_->connectComponent(PORT_CLOCK_VIDEO,
                        video_scheduler_,
                        video_scheduler_->getClockInputPortIndex()),
                    "Load - tunneling between clock and video scheduler");
            BREAK_IF_NONZERO(video_scheduler_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
                    "setting video scheduler to idle state");

            BREAK_IF_NONZERO(video_codec_->setupTunnel(video_codec_->getOutputPortIndex(),
                        video_scheduler_.get(), video_scheduler_->getInputPortIndex()),
                    "Load - tunneling between decoder and scheduler");
            BREAK_IF_NONZERO(video_scheduler_->setupTunnel(video_scheduler_->getOutputPortIndex(),
                        video_renderer_.get(), video_renderer_->getInputPortIndex()),
                    "Load - tunneling between scheduler and renderer");

            // video renderer need over 1sec for change state to idle.
            LOG_IF_NONZERO(video_renderer_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME+1),
                    "setting video renderer to idle state");

            enable_video_tunnel_ = true;

            //set up video decoder configure
            OMX_CONFIG_REQUESTCALLBACKTYPE notifications;
            omx_init_structure(&notifications, OMX_CONFIG_REQUESTCALLBACKTYPE);
            notifications.nPortIndex = video_codec_->getOutputPortIndex();
            notifications.nIndex = OMX_IndexParamBrcmPixelAspectRatio;
            notifications.bEnable = OMX_TRUE;
            LOG_IF_NONZERO(video_codec_->setParam((OMX_INDEXTYPE)OMX_IndexConfigRequestCallback, &notifications),
                    "Load - set video decoder output OMX_IndexConfigRequestCallback");

            OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE concanParam;
            omx_init_structure(&concanParam,OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE);
            concanParam.bStartWithValidFrame = OMX_TRUE;
            LOG_IF_NONZERO(video_codec_->setParam((OMX_INDEXTYPE)OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concanParam),
                    "Load - set video decoder input OMX_IndexParamBrcmVideoDecodeErrorConcealment");
        }

        if (enable_audio_) {
            //Create Broadcom Audio Decoder
            result = NDL_ESP_RESULT_AUDIO_UNSUPPORTED;
            BREAK_IF_NONZERO(loadAudioComponents(meta->audio_codec),
                    "creating audio codec components");

            result = NDL_ESP_RESULT_AUDIO_CODEC_ERROR;
            BREAK_IF_NONZERO( audio_codec_ &&
                    audio_codec_->setAudioCodecFormat(audio_codec_->getInputPortIndex(),
                        meta),
                    "setting audio codec format");

            // configure audio codec input port buffers
            BREAK_IF_NONZERO( audio_codec_ &&
                    audio_codec_->configureInputBuffers(audio_codec_->getInputBufferCount(),
                        audio_renderer_->getInputBufferSize()),
                    "reconfiguring audio codec  input buffer");

            // configure audio codec output port buffers
            BREAK_IF_NONZERO( audio_codec_ &&
                    audio_codec_->configureOutputBuffers(audio_codec_->getOutputBufferCount(),
                        audio_renderer_->getInputBufferSize()),
                    "reconfiguring audio codec  Output buffer");

            // go to idle state
            result = NDL_ESP_RESULT_AUDIO_STATE_ERROR;
            BREAK_IF_NONZERO(audio_codec_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
                    "setting audio decoder to idle state and wait done");

            // Allocate audio codec  input buffer
            result = NDL_ESP_RESULT_AUDIO_BUFFER_ERROR;
            BREAK_IF_NONZERO(audio_codec_->allocateInputBuffer(),
                    "allocating audio decoder input buffers");
            BREAK_IF_NONZERO(audio_codec_->waitForPortEnable(audio_codec_->getInputPortIndex(), true, MAX_PORT_WAIT_TIME),
                    "waitForPortEnable for audio decoder");

            result = NDL_ESP_RESULT_AUDIO_STATE_ERROR;
            BREAK_IF_NONZERO(audio_codec_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "setting audio decoder to executing state and wait done");

            result = NDL_ESP_RESULT_CLOCK_STATE_ERROR;
            BREAK_IF_NONZERO(clock_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "setting clock to executing state and wait done");

            // connect clock to audio render. It should be done under loadstate of audio_renderer
            result = NDL_ESP_RESULT_CLOCK_ERROR;
            BREAK_IF_NONZERO(clock_->connectComponent(PORT_CLOCK_AUDIO,
                        audio_renderer_,
                        audio_renderer_->getClockInputPortIndex()),
                    "tunneling between clock and audio renderer");

            // setup audiopcm info for audio mixer and audio renderer
            // TODO: make it function.
            OMX_AUDIO_PARAM_PCMMODETYPE pcm;
            memset(&pcm, 0, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
            omx_init_structure(&pcm, OMX_AUDIO_PARAM_PCMMODETYPE);
            pcm.nPortIndex = audio_codec_->getOutputPortIndex();
            LOG_AND_RETURN_IF_NONZERO(audio_codec_->getParam(OMX_IndexParamAudioPcm, &pcm),
                    "Load: get audio codec PCM format");

            NDLLOG(LOGTAG, NDL_LOGE, "Load - set PCM : %dkHz %dch %dbits, unsigned=%d, little endian=%d",
                    (int)pcm.nSamplingRate, (int)pcm.nChannels, (int)pcm.nBitPerSample,
                    (int)pcm.eNumData, (int)pcm.eEndian);

            pcm.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
            pcm.nSamplingRate = 44100;//std::min(std::max((int)pcm.nSamplingRate, 8000), 192000);
            pcm.nBitPerSample = 16;
            pcm.eEndian = OMX_EndianLittle;
            pcm.eNumData = OMX_NumericalDataSigned;
            pcm.bInterleaved = OMX_TRUE;
            pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;
            pcm.nChannels = meta_.channels;
            pcm.nVersion.nVersion= OMX_VERSION;

            pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
            pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;

#if SUPPORT_AUDIOMIXER
            pcm.nPortIndex = audio_mixer_->getOutputPortIndex();
            LOG_IF_NONZERO(audio_mixer_->setParam(OMX_IndexParamAudioPcm, &pcm),
                    "Load: set audio mixer PCM format");
#endif
            //audio renderer input port is already enabled during setupTunnel
            pcm.nPortIndex = audio_renderer_->getInputPortIndex();
            LOG_IF_NONZERO(audio_renderer_->setParam(OMX_IndexParamAudioPcm, &pcm),
                    "Load: set audio renderer PCM format");

            /* setup audio tunneling
            ** audio_codec_ -> audio_mixer_ -> audio_renderer
            **                                       ┖clock_
            */
#if SUPPORT_AUDIOMIXER
            result = NDL_ESP_RESULT_CLOCK_ERROR;
            BREAK_IF_NONZERO(clock_->connectComponent(PORT_CLOCK_AUDIO_MIXER,
                        audio_mixer_,
                        audio_mixer_->getClockInputPortIndex()),
                    "tunneling between clock and audio renderer");

            result = NDL_ESP_RESULT_AUDIO_TUNNEL_ERROR;
            if (!audio_codec_->setupTunnel(audio_codec_->getOutputPortIndex(),
                        audio_mixer_.get(), audio_mixer_->getInputPortIndex())) {
                BREAK_IF_NONZERO(audio_mixer_->waitForPortEnable(audio_mixer_->getInputPortIndex(), true, MAX_PORT_WAIT_TIME),
                        "waitForPortEnable for audio mixer");

                result = NDL_ESP_RESULT_AUDIO_STATE_ERROR;
                BREAK_IF_NONZERO(audio_mixer_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
                        "setting audio mixer to idle state and wait done");

                BREAK_IF_NONZERO(audio_codec_->waitForPortEnable(audio_codec_->getOutputPortIndex(), true, MAX_PORT_WAIT_TIME),
                        "waitForPortEnable for audio decoder");
            }

            result = NDL_ESP_RESULT_AUDIO_TUNNEL_ERROR;
            if (!audio_mixer_->setupTunnel(audio_mixer_->getOutputPortIndex(),
                        audio_renderer_.get(), audio_renderer_->getInputPortIndex())){
                BREAK_IF_NONZERO(audio_renderer_->waitForPortEnable(audio_renderer_->getInputPortIndex(), true, MAX_PORT_WAIT_TIME),
                        "waitForPortEnable for audio decoder");

                result = NDL_ESP_RESULT_AUDIO_STATE_ERROR;
                BREAK_IF_NONZERO(audio_renderer_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
                        "setting audio renderer to idle state and wait done");

                BREAK_IF_NONZERO(audio_mixer_->waitForPortEnable(audio_mixer_->getOutputPortIndex(), true, MAX_PORT_WAIT_TIME),
                        "waitForPortEnable for audio decoder");
                enable_audio_tunnel_ = true;
            }
            result = NDL_ESP_RESULT_AUDIO_STATE_ERROR;
            BREAK_IF_NONZERO(audio_mixer_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "setting audio mixer to executing state and wait done");

            BREAK_IF_NONZERO(audio_renderer_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "setting audio renderer to executing state and wait done");
#else
            if (!audio_codec_->setupTunnel(audio_codec_->getOutputPortIndex(),
                            audio_renderer_.get(), audio_renderer_->getInputPortIndex())) {
                BREAK_IF_NONZERO(audio_renderer_->waitForPortEnable(audio_renderer_->getInputPortIndex(), true, MAX_PORT_WAIT_TIME),
                        "waitForPortEnable for audio renderer");

                result = NDL_ESP_RESULT_AUDIO_STATE_ERROR;
                BREAK_IF_NONZERO(audio_renderer_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
                        "setting audio renderer to idle state and wait done");

                BREAK_IF_NONZERO(audio_codec_->waitForPortEnable(audio_codec_->getOutputPortIndex(), true, MAX_PORT_WAIT_TIME),
                        "waitForPortEnable for audio decoder");
                enable_audio_tunnel_ = true;
            }
            result = NDL_ESP_RESULT_AUDIO_STATE_ERROR;
            BREAK_IF_NONZERO(audio_renderer_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "setting audio renderer to executing state and wait done ");
#endif
            /* In RPi, components are tunneled as below
               audio_codec -> audio_mixer -> audio_render <- clock */

            OMX_CONFIG_BOOLEANTYPE configBool;
            omx_init_structure(&configBool, OMX_CONFIG_BOOLEANTYPE);
            configBool.bEnabled = /*m_config.is_live ? OMX_FALSE:*/OMX_TRUE;
            BREAK_IF_NONZERO( audio_renderer_->setConfig(OMX_IndexConfigBrcmClockReferenceSource, &configBool),"OMX_IndexConfigBrcmClockReferenceSource");

            OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
            omx_init_structure(&audioDest, OMX_CONFIG_BRCMAUDIODESTINATIONTYPE);
            strncpy((char *)audioDest.sName, AUDIO_OUTPUT_DESTINATION, strlen(AUDIO_OUTPUT_DESTINATION));
            BREAK_IF_NONZERO( audio_renderer_->setConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest),"OMX_IndexConfigBrcmAudioDestination");
        }

        if( clock_ ) {
            NDLLOG(SDETTAG, NDL_LOGI, "%s : updateWaitMaskForStartTime", __func__);
            if (enable_video_ && enable_audio_
                    && (trick_mode_enabled_|| target_playback_rate_!=Clock::NORMAL_PLAYBACK_RATE)) {
                BREAK_IF_NONZERO(clock_->setWaitingForStartTime(PORT_CLOCK_AUDIO), "setWaitingForStartTime");
            }
            else
            {
                int port_mask = 0;
                if( enable_audio_ )
                    port_mask = 1;
                if( enable_video_ )
                    port_mask |= 2;
                BREAK_IF_NONZERO(clock_->setWaitingForStartTime(port_mask), "setWaitingForStartTime mask:%d", port_mask);
            }
        }

        result = NDL_ESP_RESULT_SUCCESS;
        state_.transit(NDL_ESP_STATUS_LOADED);
    } while(0);

    if( result == NDL_ESP_RESULT_SUCCESS ) {
        printComponentInfo();
        if(enable_audio_) {
            audio_codec_->getPortDefinition(audio_codec_->getInputPortIndex());
            audio_codec_->getPortDefinition(audio_codec_->getOutputPortIndex());
#if SUPPORT_AUDIOMIXER
            audio_mixer_->getPortDefinition(audio_mixer_->getInputPortIndex());
            audio_mixer_->getPortDefinition(audio_mixer_->getOutputPortIndex());
#endif
            audio_renderer_->getPortDefinition(audio_renderer_->getInputPortIndex());
        }
        if(enable_video_){
            video_codec_->getPortDefinition(video_codec_->getInputPortIndex());
            video_codec_->getPortDefinition(video_codec_->getOutputPortIndex());
            video_scheduler_->getPortDefinition(video_scheduler_->getInputPortIndex());
            video_scheduler_->getPortDefinition(video_scheduler_->getOutputPortIndex());
            video_renderer_->getPortDefinition(video_renderer_->getInputPortIndex());
        }
    }

#ifdef FILEDUMP
    GENERATE_FILE_NAMES();
    CREATE_DUMP_FILE(mInFile);
    CREATE_DUMP_FILE(mOutFile);
#endif

    NDLLOG(SDETTAG, LOG_INOUT, "%s result:%d -", __func__, result);
    return result;
}

void Esplayer::sendVideoDecoderConfig()
{
    NDLLOG(SDETTAG, LOG_INOUT, "%s +", __func__);

    int write_len = video_codec_->writeToConfigBuffer(video_codec_->getInputPortIndex(),
            (uint8_t*)meta_.extradata,
            meta_.extrasize);

    NDLLOG(SDETTAG, LOG_INOUT, "%s write_len:%d -", __func__, write_len);
}

void Esplayer::sendAudioDecoderConfig()
{
    NDLLOG(SDETTAG, LOG_INOUT, "%s +", __func__);
    memset(&m_wave_header, 0x0, sizeof(WAVEFORMATEXTENSIBLE));

    m_wave_header.Format.nChannels  = meta_.channels;
    m_wave_header.dwChannelMask     = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    m_wave_header.Samples.wSamplesPerBlock    = 0;
    m_wave_header.Format.nChannels            = meta_.channels;
    m_wave_header.Format.nBlockAlign          = meta_.channels *    (meta_.bitspersample >> 3);
    // 0x8000 is custom format interpreted by GPU as WAVE_FORMAT_IEEE_FLOAT_PLANAR
    m_wave_header.Format.wFormatTag           = meta_.bitspersample == 32 ? 0x8000 : WAVE_FORMAT_PCM;
    m_wave_header.Format.nSamplesPerSec       = meta_.samplerate;
    m_wave_header.Format.nAvgBytesPerSec      = meta_.samplerate * 2 << rounded_up_channels_shift[meta_.channels];
    m_wave_header.Format.wBitsPerSample       = meta_.bitspersample;
    m_wave_header.Samples.wValidBitsPerSample = meta_.bitspersample;
    m_wave_header.Format.cbSize               = 0;
    m_wave_header.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;

    uint8_t *pcmHeader = new uint8_t[sizeof(m_wave_header)];
    memcpy((uint8_t *)pcmHeader, &m_wave_header, sizeof(m_wave_header));
    int write_len = audio_codec_->writeToConfigBuffer(audio_codec_->getInputPortIndex(),
            (uint8_t*)pcmHeader,
            sizeof(WAVEFORMATEXTENSIBLE));
    delete pcmHeader;
    NDLLOG(SDETTAG, LOG_INOUT, "%s write_len:%d -", __func__, write_len);
}

int Esplayer::loadEx(NDL_ESP_META_DATA* meta, NDL_ESP_PTS_UNITS units)
{
    pts_units_ = units;
    return load(meta);
}

int Esplayer::loadClockComponent()
{
    auto clock = std::make_shared<OmxClock>();

    if (clock->create() != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating clock");
        return NDL_ESP_RESULT_CLOCK_ERROR;
    }

    clock_ = clock;

    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::loadVideoComponents(NDL_ESP_VIDEO_CODEC codec)
{
    auto decoder = std::make_shared<Component>(
            [this] (int event, uint32_t data1, uint32_t data2, void* data) {
            onVideoCodecCallback(event, data1, data2, data);
            });

    if (decoder->create(codec) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating video decoder");
        return NDL_ESP_RESULT_VIDEO_CODEC_ERROR;
    }
    video_codec_ = decoder;

    auto renderer = std::make_shared<Component>(
            [this] (int event, uint32_t data1, uint32_t data2, void* data) {
#ifdef OMX_NONE_TUNNEL
            onVideoRendererCallback(event, data1, data2, data);
#endif
            });
    if (renderer->create(Component::VIDEO_RENDERER) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating video renderer");
        return NDL_ESP_RESULT_VIDEO_RENDER_ERROR;
    }
    video_renderer_ = renderer;

    auto scheduler = std::make_shared<Component>(
            [this] (int event, uint32_t data1, uint32_t data2, void* data) {
#ifdef OMX_NONE_TUNNEL
            onVideoSchedulerCallback(event, data1, data2, data);
#endif
            });
    if (scheduler->create(Component::VIDEO_SCHEDEULER) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating video scheduler");
        return NDL_ESP_RESULT_VIDEO_RENDER_ERROR;
    }
    video_scheduler_ = scheduler;

    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::loadAudioComponents(NDL_ESP_AUDIO_CODEC codec)
{
    // no codec for pcm
    // TODO: is that right using NDL_ESP_AUDIO_CODEC as a parameter,
    //  even there is no codec for pcm?
    auto audio_decoder = std::make_shared<Component>(
            [this] (int event, uint32_t data1, uint32_t data2, void* data) {
            onAudioCodecCallback(event, data1, data2, data);
            });

    if (audio_decoder->create(codec) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating audio decoder");
        return NDL_ESP_RESULT_AUDIO_CODEC_ERROR;
    }
    audio_codec_ = audio_decoder;

    auto renderer = std::make_shared<Component>(
            [this] (int event, uint32_t data1, uint32_t data2, void* data) {
#ifdef OMX_NONE_TUNNEL
            onAudioRendererCallback(event, data1, data2, data);
#endif
            });

    if (renderer->create(Component::AUDIO_RENDERER) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating audio renderer");
        return NDL_ESP_RESULT_AUDIO_RENDER_ERROR;
    }
    audio_renderer_ = renderer;

#if SUPPORT_AUDIOMIXER
    auto mixer = std::make_shared<Component>(
            [this] (int event, uint32_t data1, uint32_t data2, void* data) {
#ifdef OMX_NONE_TUNNEL
            onAudioMixerCallback(event, data1, data2, data);
#endif
            });

    if (mixer->create(Component::AUDIO_MIXER) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating audio mixer");
        return NDL_ESP_RESULT_AUDIO_RENDER_ERROR;
    }
    audio_mixer_= mixer;
#endif
    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::unload()
{
    NDLASSERT(state_.canTransit(NDL_ESP_STATUS_UNLOADED));
    NDLLOG(SDETTAG, LOG_INOUT, "%s +", __func__);
    if (!state_.canTransit(NDL_ESP_STATUS_UNLOADED)) {
        NDLLOG(LOGTAG, NDL_LOGE, "unsupported operation, current state:%d", state_.get());
        return NDL_ESP_RESULT_FAIL;
    }

    std::lock_guard<std::mutex> lock(unload_mutex_);
    if (!loaded_.exchange(false)) {
        NDLLOG(LOGTAG, NDL_LOGI, "block duplicate unload call");
        return NDL_ESP_RESULT_SUCCESS;
    }

    //clear message looper
    clearFrameQueues();

    callback_ = 0;
    userdata_ = 0;

    //Step 1. Flush all component(already done)
    //Step 2. Disable output port and free output buffer
    //Step 3. Disable input port and free input buffer
    LOG_IF_NONZERO(video_renderer_&&
            video_renderer_->enablePort(video_renderer_->getInputPortIndex(), false, 0/*MAX_PORT_WAIT_TIME*/),
            "waitForPortDisable for output Port of video renderer");

    LOG_IF_NONZERO(video_scheduler_&&
            video_scheduler_->enablePort(video_scheduler_->getOutputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortDisable for output Port of video scheduler");

    LOG_IF_NONZERO(video_scheduler_&&
            video_scheduler_->enablePort(video_scheduler_->getInputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortDisable for input Port of video scheduler");

    LOG_IF_NONZERO(video_codec_&&
            video_codec_->enablePort(video_codec_->getOutputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortDisable for output Port of video decoder");

    // Free video input buffer and disable input port
    LOG_IF_NONZERO(video_codec_&&
            video_codec_->freeBuffer(video_codec_->getInputPortIndex()),
            "free video decoder input buffer and disable input port");

    // FreeBuffer set port configuration so wait it's completion
    LOG_IF_NONZERO(video_codec_&&
            video_codec_->enablePort(video_codec_->getInputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortEnable for input Port of video decoder");

#if 0 //timeout always
    LOG_IF_NONZERO(audio_renderer_&&
            audio_renderer_->enablePort(audio_renderer_->getInputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortDisable for input port of audio renderer");
#endif

#if SUPPORT_AUDIOMIXER
    LOG_IF_NONZERO(audio_mixer_&&
            audio_mixer_->enablePort(audio_mixer_->getOutputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortDisable for output port of audio mixer");

    LOG_IF_NONZERO(audio_mixer_&&
            audio_mixer_->enablePort(audio_mixer_->getInputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortDisalbe for input port of audio mixer");
#endif

    LOG_IF_NONZERO(audio_codec_&&
            audio_codec_->enablePort(audio_codec_->getOutputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortDisable for output Port of audio decoder");

    // Free audio input buffer and disable input port
    LOG_IF_NONZERO(audio_codec_&&
            audio_codec_->freeBuffer(audio_codec_->getInputPortIndex()),
            "free audio decoder input buffer and disable input port");

    // FreeBuffer set port configuration so wait it's completion
    LOG_IF_NONZERO(audio_codec_&&
            audio_codec_->waitForPortEnable(audio_codec_->getInputPortIndex(), false, MAX_PORT_WAIT_TIME),
            "waitForPortEnable for input Port of audio decoder");

    // video_renderer state change execute->idle->load for destroy component
    LOG_IF_NONZERO(video_renderer_&&video_renderer_->setState(OMX_StateIdle, MAX_STATE_WAIT_TIME),
            "change video_renderer state to idle");

    LOG_IF_NONZERO(video_renderer_&&video_renderer_->setState(OMX_StateLoaded, MAX_STATE_WAIT_TIME),
            "change video_renderer state to load");

    // we can disable audio/video clock port when idle state.
    LOG_IF_NONZERO(clock_&&
            clock_->enablePort(PORT_CLOCK_VIDEO, false, 0),
            "disable video clock port");
    LOG_IF_NONZERO(clock_&&
            clock_->enablePort(PORT_CLOCK_AUDIO, false, 0),
            "disable audio clock port");

#ifdef SUPPORT_AUDIOMIXER
    LOG_IF_NONZERO(clock_&&
            clock_->enablePort(PORT_CLOCK_AUDIO_MIXER, false, 0),
            "disable audio clock port");
#endif

    NDLLOG(LOGTAG, NDL_LOGI, "destroy all components +");

    if (video_codec_) {
        NDLLOG(LOGTAG, NDL_LOGI, "destroy video_codec_");
        video_codec_->destroy();
        video_codec_ = nullptr;
    }
    if (video_renderer_) {
        NDLLOG(LOGTAG, NDL_LOGI, "destroy video_renderer_");
        video_renderer_->destroy();
        video_renderer_ = nullptr;
    }

    if (video_scheduler_) {
        NDLLOG(LOGTAG, NDL_LOGI, "destroy video_scheduler_");
        video_scheduler_->destroy();
        video_scheduler_ = nullptr;
    }

#if SUPPORT_AUDIOMIXER
    if (audio_mixer_) {
        NDLLOG(LOGTAG, NDL_LOGI, "destroy audio_mixer_");
        audio_mixer_->destroy();
        audio_mixer_ = nullptr;
    }
#endif

    if (audio_codec_) {
        NDLLOG(LOGTAG, NDL_LOGI, "destroy audio_codec_");
        audio_codec_->destroy();
        audio_codec_ = nullptr;
    }
    if (audio_renderer_) {
        NDLLOG(LOGTAG, NDL_LOGD, "destroy audio_renderer_");
        audio_renderer_->destroy();
        audio_renderer_ = nullptr;
    }
    if (clock_) {
        NDLLOG(LOGTAG, NDL_LOGD, "destroy clock_");
        clock_->destroy();
        clock_ = nullptr;
    }

    NDLLOG(LOGTAG, NDL_LOGD, "destroy all components =");

    state_.transit(NDL_ESP_STATUS_UNLOADED);
    rm_->enableScreenSaver();
    rm_->releaseResource();

    NDLLOG(SDETTAG, LOG_INOUT, "%s -", __func__);

    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::getConnectionId(char* buf, size_t buf_len) const
{
    if (buf==NULL || buf_len <= connectionId_.size() || connectionId_.empty()) {
        return NDL_ESP_RESULT_FAIL;
    }

    size_t copylength = connectionId_.copy(buf, connectionId_.size());
    buf[copylength] = '\0';
    return NDL_ESP_RESULT_SUCCESS;
}

const std::string& Esplayer::getConnectionId() const
{
    return connectionId_;
}

int Esplayer::changeComponentsState(OMX_STATETYPE state, int timeout_seconds)
{
    NDLLOG(LOGTAG, LOG_INOUT, "%s change to state:%d (wait time:%d) +", __func__, state, timeout_seconds);

    LOG_AND_RETURN_IF_NONZERO(video_codec_&&video_codec_->setState(state, timeout_seconds),
            "change video_codec state");

    // DRM video renderer doesn't support PAUSE state.
    if (state != OMX_StatePause)
        LOG_AND_RETURN_IF_NONZERO(video_renderer_&&video_renderer_->setState(state, timeout_seconds),
                "change video_renderer state");

    LOG_AND_RETURN_IF_NONZERO(audio_codec_&&audio_codec_->setState(state, timeout_seconds),
            "change audio_codec state");

#if SUPPORT_ALSA_RENDERER_COMPONENT
    // ALSA audio renderer doesn't support PAUSE and some state.
    if (state != OMX_StatePause)
#endif
    LOG_AND_RETURN_IF_NONZERO(audio_renderer_&&audio_renderer_->setState(state, timeout_seconds),
            "change audio_renderer state");

#if SUPPORT_AUDIOMIXER
    LOG_AND_RETURN_IF_NONZERO(audio_mixer_&&audio_mixer_->setState(state, timeout_seconds),
            "change audio_mixer_ state");
#endif

    LOG_AND_RETURN_IF_NONZERO(video_scheduler_&&video_scheduler_->setState(state, timeout_seconds),
            "change video_scheduler_ state");

    LOG_AND_RETURN_IF_NONZERO(clock_&&clock_->setState(state, timeout_seconds),
            "change clock state");

    NDLLOG(LOGTAG, LOG_INOUT, "%s -", __func__);
    return NDL_ESP_RESULT_SUCCESS;
}

// some component doesn't support OMX_ALL. So we have to flush input/output explicitly
int Esplayer::setComponentsFlush(int timeout_seconds)
{
    NDLLOG(LOGTAG, LOG_INOUT, "%s(wait:%d) +", __func__, timeout_seconds);

    LOG_IF_NONZERO(video_codec_&&video_codec_->flush(OMX_ALL, timeout_seconds),
            "flush done video_codec");

    LOG_IF_NONZERO(video_scheduler_&&video_scheduler_->flush(video_scheduler_->getInputPortIndex(), timeout_seconds),
            "flush done video_scheduler input port");


    LOG_IF_NONZERO(video_renderer_&&video_renderer_->flush(video_renderer_->getInputPortIndex(), timeout_seconds),
            "flush done video_renderer input port");

    LOG_IF_NONZERO(audio_codec_&&audio_codec_->flush(OMX_ALL, timeout_seconds),
            "flush done audio_codec");

#if SUPPORT_AUDIOMIXER
    LOG_IF_NONZERO(audio_mixer_&&audio_mixer_->flush(OMX_ALL, timeout_seconds),
            "flush done audio_mixer");
#endif

    LOG_IF_NONZERO(audio_renderer_&&
            audio_renderer_->flush(audio_renderer_->getInputPortIndex(), timeout_seconds),
            "flush done audio_renderer inut port");

    NDLLOG(LOGTAG, LOG_INOUT, "%s -", __func__);
    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::notifyForegroundState(const NDL_ESP_APP_STATE appState)
{

    // notify RM of Foreground/Background
    if (appState == NDL_ESP_APP_STATE_BACKGROUND) {
        foreground_ = false;
        return rm_->notifyBackground() ? NDL_ESP_RESULT_SUCCESS : NDL_ESP_RESULT_SET_STATE_ERROR;
    }
    else if (appState == NDL_ESP_APP_STATE_FOREGROUND) {
        foreground_ = true;
        return rm_->notifyForeground() ? NDL_ESP_RESULT_SUCCESS : NDL_ESP_RESULT_SET_STATE_ERROR;
    }

    return NDL_ESP_RESULT_SUCCESS;
}

int64_t Esplayer::adjustPtsToMicrosecond(int index, int64_t pts)
{
    const int64_t max = ((int64_t)1<<33) - 1; // 2^33-1
    const int64_t big = ((int64_t)1<<32) - 1; // 2^32-1
    int64_t ret_pts = 0;

    if (pts_previous_[index] == -1) {
        int port_index = (index==/*PORT_CLOCK_VIDEO*/0) ? /*PORT_CLOCK_AUDIO*/1 : /*PORT_CLOCK_VIDEO*/0;
        if ((pts_previous_[port_index] !=-1) && (std::abs(pts_previous_[port_index] - pts) >= big)) {
            // Audio/video pts wrap around occurs with different timing independently.
            //   So, if input stream starts in the middle of wraparounds,
            //   there'll be huge gap between audio and video pts.
            // Therefore, if diff between the previous pts of the other port and the first pts of this port is big enough,
            // assume that there was a wraparound before.
            //
            NDLLOG(LOGTAG, NDL_LOGI, "already wraparound before... Port : %d, %lld vs %lld ",
                    port_index, pts_previous_[port_index], pts);
            if (pts_previous_[port_index] > pts)
                port_index = index;

            pts_base_[port_index] += max;
        }
    }

    if (pts_previous_[index] - pts > big) { // pts drop detected
        if (pts_drop_count_[index] < PTS_DROP_THRESHOLD) {
            // just adjust pts, don't increase pts_base_
            //  don't save current pts here (it'll generate next drop even if there's no real drop)
            pts_drop_count_[index] += 1;
            NDLLOG(LOGTAG, NDL_LOGI, "pts[%d] drop count : %d, %lld->%lld, pts_base_[%d]:%lld",
                    index, pts_drop_count_[index], pts_previous_[index], pts, index, pts_base_[index]);
            pts += max;
        } else {
            // submit pts wraparound
            pts_drop_count_[index] = 0;
            pts_base_[index] += max;
            NDLLOG(LOGTAG, NDL_LOGI, "pts[%d] : 2nd drop detected, %lld->%lld, new pts_base_[%d]:%lld",
                    index, pts_previous_[index], pts, index, pts_base_[index]);
            pts_previous_[index] = pts;
        }
    } else {
        pts_drop_count_[index] = 0;
        pts_previous_[index] = pts;
    }

    // in the NDL_EPS_PTS_TICKS case PTS is in 90kHz, 1 pts = 1000000 / 90000 microsecods
    ret_pts = (pts_units_ == NDL_ESP_PTS_TICKS ? (pts+pts_base_[index])*100/9 : pts+pts_base_[index]);
    //NDLLOG(LOGTAG, LOG_FEEDINGV, "%s(idx:%d),  pts:%lld, ret_pts:%lld", __func__, index, pts, ret_pts);
    return ret_pts;
}

uint32_t Esplayer::setOmxFlags(int64_t pts, uint32_t buffer_flags, NDL_ESP_STREAM_T stream_type)
{
    int video_used_buffer_count = video_codec_->getUsedBufferCount(video_codec_->getInputPortIndex());
    int audio_used_buffer_count = audio_codec_->getUsedBufferCount(audio_codec_->getInputPortIndex());
    int video_render_qsize = video_renderer_looper_.size();
    int audio_render_qsize = audio_renderer_looper_.size();
    int64_t av_delta = 0;

    syncState sync_state_new = sync_state_;

    if (stream_type == NDL_ESP_VIDEO_ES) {
        if( video_last_pts_ != 0 && audio_last_pts_ != 0 )
            av_delta = video_last_pts_ - audio_last_pts_;

        if (video_last_pts_ == 0) {
            NDLLOG(LOGTAG, LOG_FEEDING, "%s, ALLOW_VIDEO at starttime in pts 0", __func__);
            sync_state_ = ALLOW_VIDEO;
            buffer_flags = buffer_flags | OMX_BUFFERFLAG_STARTTIME;
        }
        else {
            if (av_delta < VIDEO_IN_BUFFER_MKSEC_SKIP) {
                sync_state_ = SKIP_VIDEO;
                NDLLOG(LOGTAG, LOG_FEEDINGV, "SKIP_VIDEO av_delta:%lld, pts:%lld", av_delta, pts);
                buffer_flags = buffer_flags | OMX_BUFFERFLAG_DECODEONLY;
            } else if ((sync_state_ != HOLD_VIDEO) && (av_delta > VIDEO_IN_BUFFER_MKSEC_HIGH)) {
                sync_state_new = HOLD_VIDEO;
                NDLLOG(LOGTAG, LOG_FEEDINGV, "HOLD_VIDEO high av_delta:%lld, video_used_buf_cnt:%d", av_delta, video_used_buffer_count);
            } else if (sync_state_ == SKIP_VIDEO && av_delta < VIDEO_IN_BUFFER_MKSEC_LOW) {
                sync_state_new = ALLOW_VIDEO;
                NDLLOG(LOGTAG, LOG_FEEDING, "%s, ALLOW_VIDEO in SKIP", __func__);
            }
        }

        if ((sync_state_ == ALLOW_VIDEO) && ( video_used_buffer_count > VIDEO_IN_BUFFER_COUNT_HIGH)) {
            NDLLOG(LOGTAG, LOG_FEEDINGV, "HOLD_VIDEO 2 av_delta:%lld, video_used_buf_cnt:%d, qsize:%d",
                    av_delta, video_used_buffer_count, video_render_qsize);
            sync_state_new = HOLD_VIDEO;
        }
        video_last_pts_ = pts;
    }
    else if (stream_type == NDL_ESP_AUDIO_ES) {
        if( video_last_pts_ != 0 && audio_last_pts_ != 0 )
            av_delta = video_last_pts_ - audio_last_pts_;

        if (audio_last_pts_ == 0){
            NDLLOG(LOGTAG, LOG_FEEDING, "%s, AUDIO set starttime flag in pts 0", __func__);
            buffer_flags = buffer_flags | OMX_BUFFERFLAG_STARTTIME;
        }

        if ((sync_state_ != ALLOW_VIDEO)
                && ( av_delta < VIDEO_IN_BUFFER_MKSEC_LOW && av_delta > VIDEO_IN_BUFFER_MKSEC_SKIP )
                && (video_used_buffer_count <= VIDEO_IN_BUFFER_COUNT_HIGH)){
            sync_state_new = ALLOW_VIDEO;
            NDLLOG(LOGTAG, LOG_FEEDINGV, "ALLOW_VIDEO by audio ES. av_delta:%lld", av_delta);
        }
        audio_last_pts_ = pts;
    }
    else {
        NDLLOG(LOGTAG, LOG_FEEDING, "Wrong stream type:%d", stream_type);
    }

    if (sync_state_ != HOLD_VIDEO && (sync_state_new == HOLD_VIDEO)) {
        sync_state_ = HOLD_VIDEO;
        NDLLOG(LOGTAG, LOG_FEEDING, "notifyClient PTS HOLD_VIDEO (Apts:%lld/Vpts:%lld, delta:%d)(used a:%d/v:%d)",
                audio_last_pts_, video_last_pts_, av_delta, video_used_buffer_count, audio_used_buffer_count);
        video_message_looper_.post(std::make_shared<Message>([this]{
                    //NDLLOG(LOGTAG, NDL_LOGD, "notifyClient NDL_ESP_HIGH_THRESHOLD_CROSSED_VIDEO by PTS");
                    notifyClient(NDL_ESP_HIGH_THRESHOLD_CROSSED_VIDEO);
                    return NDL_ESP_RESULT_SUCCESS;
                    }));
    } else if (sync_state_ == HOLD_VIDEO && sync_state_new == ALLOW_VIDEO) {
        sync_state_ = ALLOW_VIDEO;
        NDLLOG(LOGTAG, LOG_FEEDING, "notifyClient PTS ALLOW_VIDEO (Apts:%lld/Vpts:%lld, delta:%d)(used a:%d/v:%d)",
                audio_last_pts_, video_last_pts_, av_delta, video_used_buffer_count, audio_used_buffer_count);
        video_message_looper_.post(std::make_shared<Message>([this]{
                    //NDLLOG(LOGTAG, NDL_LOGD, "notifyClient NDL_ESP_LOW_THRESHOLD_CROSSED_VIDEO by PTS");
                    notifyClient(NDL_ESP_LOW_THRESHOLD_CROSSED_VIDEO);
                    return NDL_ESP_RESULT_SUCCESS;
                    }));
    }
    return buffer_flags;
}

int Esplayer::Feed_AudioData(void)
{
    int written_len = 0;
    uint32_t buffer_flags = 0;
    uint8_t* data = nullptr;
    int32_t data_len = 0;
    int ret = 0;
    const NDL_ESP_STREAM_T stream_type = NDL_ESP_AUDIO_ES;

    if(state_.get() != NDL_ESP_STATUS_PLAYING)
        std::lock_guard<std::mutex> lock(unload_mutex_);

    if (!audio_codec_) {
        clearBufQueue(stream_type);
        NDLLOG(LOGTAG, NDL_LOGD, "%s, codec is null", __func__);
        return written_len;
    }

    if (audio_codec_->getFreeBufferCount(audio_codec_->getInputPortIndex()) == 0) {
        NDLLOG(LOGTAG, NDL_LOGV, "Audio buffer full in feeder(u:%d,f:%d)",
                audio_codec_->getUsedBufferCount(audio_codec_->getInputPortIndex()),
                audio_codec_->getFreeBufferCount(audio_codec_->getInputPortIndex()));
        return NDL_ESP_RESULT_FEED_FULL;//buffer full
    }

    NDL_ESP_STREAM_BUFFER* buff = getBufQueue(stream_type);
    if( buff == nullptr ) {
        NDLLOG(LOGTAG, NDL_LOGD, "%s, buffer is null", __func__);
        return written_len;
    }

    NDLLOG(LOGTAG, LOG_FEEDINGV, "%s, pts:%lld  data_size:%d (qsize:%d, empty_buf_size:%d)",
            __func__, buff->timestamp, buff->data_len, audio_renderer_looper_.size(), audio_codec_->getFreeBufferCount(audio_codec_->getInputPortIndex()));

    int64_t pts = enable_video_ ? adjustPtsToMicrosecond(/*PORT_CLOCK_AUDIO*/ buff->stream_type, buff->timestamp) : 0;

    data = buff->data;
    data_len = buff->data_len;

    if( data_len > 0 ) {
        buffer_flags = setOmxFlags(pts,
                translateToOmxFlags(buff->flags)|OMX_BUFFERFLAG_ENDOFFRAME,
                buff->stream_type);

        // for audio sw decoder
        if (audio_sw_decoder_ != NULL) {
            if (audio_sw_decoder_->audio_stream_info_.codec_id != AV_CODEC_ID_PCM_S16LE) {
#ifdef FILEDUMP
                DUMP_TO_FILE(mInFile, buff->data, buff->data_len);
#endif
                ret = audio_sw_decoder_->DecodeAudio(buff->data, buff->data_len, pts, pts);
                data = audio_sw_decoder_->GetOutputBufferData();
                data_len = audio_sw_decoder_->GetOutputBufferSize();
                // NDLLOG(LOGTAG, NDL_LOGD, "%s: audio frame is decoded %d(%d)", __func__, data_len, ret);
            }
        }
        else {
            NDLLOG(LOGTAG, NDL_LOGE, "%s: audio sw decoder is not created", __func__);
        }
    }
    else
        buffer_flags = translateToOmxFlags(buff->flags)|OMX_BUFFERFLAG_ENDOFFRAME;
#ifdef FILEDUMP
    DUMP_TO_FILE(mOutFile, data, data_len);
#endif

    written_len = audio_codec_->writeToFreeBuffer(audio_codec_->getInputPortIndex(),
            data,
            data_len,
            pts,
            buffer_flags);

    popBufQueue(stream_type);

    if (written_len >= 0 && ret > 0 && audio_sw_decoder_ != NULL) {
        written_len = ret;
    }

    return written_len;
}

int Esplayer::Feed_VideoData(void)
{
    int written_len = 0;
    const NDL_ESP_STREAM_T stream_type = NDL_ESP_VIDEO_ES;

    if(state_.get() != NDL_ESP_STATUS_PLAYING)
        std::lock_guard<std::mutex> lock(unload_mutex_);

    if (!video_codec_) {
        clearBufQueue(stream_type);
        NDLLOG(LOGTAG, NDL_LOGD, "%s, codec is null", __func__);
        return written_len;
    }

    NDL_ESP_STREAM_BUFFER* buff = getBufQueue(stream_type);
    if( buff == nullptr ) {
        NDLLOG(LOGTAG, NDL_LOGD, "%s, buffer is null", __func__);
        return written_len;
    }

    int32_t used_buf_cnt = video_codec_->getUsedBufferCount(video_codec_->getInputPortIndex());
    int32_t free_buf_cnt = video_codec_->getFreeBufferCount(video_codec_->getInputPortIndex());
    NDLLOG(LOGTAG, LOG_FEEDINGV, "%s pts:%lld  size:%d (u:%d/f:%d)(qsize:%d)",
                        __func__, buff->timestamp, buff->data_len, used_buf_cnt, free_buf_cnt, video_renderer_looper_.size());

    if (free_buf_cnt == 0) {
        NDLLOG(LOGTAG, NDL_LOGD, "Video buffer full in feeder(u:%d,f:%d)", used_buf_cnt, free_buf_cnt);
        return NDL_ESP_RESULT_FEED_FULL;//buffer full
    }

    int64_t pts = adjustPtsToMicrosecond(/*PORT_CLOCK_VIDEO*/ buff->stream_type, buff->timestamp);
    int remaining_buffer_size = buff->data_len;
    uint8_t* data = nullptr;
    int32_t data_len = 0;
    int32_t running_offset = 0;
    uint32_t buffer_flags = 0;

    if( buff->data_len > 0 )
        buffer_flags = setOmxFlags(pts, translateToOmxFlags(buff->flags), buff->stream_type);
    else
        buffer_flags = translateToOmxFlags(buff->flags);

    if (remaining_buffer_size > 0) {
        do {
            //sometime input chunk size is larger than codec input buffer size
            remaining_buffer_size -= video_codec_->getInputBufferSize();
            if(remaining_buffer_size > 0) {
                NDLLOG(LOGTAG, LOG_FEEDINGV, "remaining data exist! remaining size:%d, input data size:%d, codec buf size:%d pts:%lld",
                        remaining_buffer_size, buff->data_len, video_codec_->getInputBufferSize(), buff->timestamp);
            }
            data = buff->data + running_offset;

            if (remaining_buffer_size <= 0) {
                buffer_flags = buffer_flags | OMX_BUFFERFLAG_ENDOFFRAME;
                data_len = buff->data_len - running_offset;
            } else {
                data_len = video_codec_->getInputBufferSize();
                running_offset += data_len;
            }

            written_len += video_codec_->writeToFreeBuffer(video_codec_->getInputPortIndex(),
                    data,
                    data_len,
                    pts,
                    buffer_flags);
        } while (remaining_buffer_size > 0);
    } else if (remaining_buffer_size == 0) { //EOS buffer case
        data = buff->data;

        buffer_flags = buffer_flags | OMX_BUFFERFLAG_ENDOFFRAME;
        data_len = 0;

        written_len += video_codec_->writeToFreeBuffer(video_codec_->getInputPortIndex(),
                data,
                data_len,
                pts,
                buffer_flags);
    }

    popBufQueue(stream_type);

    if (written_len>=0) {
        // NDLLOG(LOGTAG, LOG_FEEDING, "feed video %6d bytes   pts:%0.f <= (%d)",
        //   data_len, buff->timestamp, buffer_flags);
    }
    return written_len;
}

int Esplayer::feedData(NDL_EsplayerBuffer buff)
{
    //struct timeval startTime, endTime;
    //int32_t elapse = 0;
    //GETTIME(&startTime, NULL);

    NDLLOG(LOGTAG, LOG_INOUT, "%s(type:%d) +", __FUNCTION__, buff->stream_type);
    NDLASSERT(buff);
    NDLASSERT(!(buff->data_len<buff->offset));
    NDLASSERT((state_.get() != NDL_ESP_STATUS_IDLE)
            &&(state_.get() != NDL_ESP_STATUS_UNLOADED)
            &&(state_.get() != NDL_ESP_STATUS_FLUSHING)
            );
    if (!buff
            || (buff->data_len<buff->offset)
       ) {
        NDLLOG(LOGTAG, NDL_LOGE, "%s, invalid input, buff:%p, data_len:%d, offset:%d",
                __FUNCTION__, buff, buff?buff->data_len:0, buff?buff->offset:0);
        return NDL_ESP_RESULT_FEED_INVALID_INPUT;
    }
    if (!loaded_
            || (state_.get() == NDL_ESP_STATUS_IDLE)
            || (state_.get() == NDL_ESP_STATUS_UNLOADED)
            || (state_.get() == NDL_ESP_STATUS_FLUSHING)
       ) {
        NDLLOG(LOGTAG, NDL_LOGE, "%s, invalid state:%d, loaded_:%d", __FUNCTION__, state_.get(), (bool)loaded_);
        return NDL_ESP_RESULT_FEED_INVALID_STATE;
    }

    printStreamBuf(__func__, buff.get());

    if(state_.get() != NDL_ESP_STATUS_PLAYING)
        std::lock_guard<std::mutex> lock(unload_mutex_);

    NDL_ESP_STREAM_T stream_type = buff->stream_type;
    pushBufQueue(buff);

    switch (stream_type)
    {
        case NDL_ESP_VIDEO_ES:
            {
                video_renderer_looper_.append(std::make_shared<Message>([this] {
                            int feed_len = Feed_VideoData();
                            if (feed_len >= 0) return NDL_ESP_RESULT_SUCCESS;
                            else               return NDL_ESP_RESULT_FAIL;
                            }));
                break;
            }
        case NDL_ESP_AUDIO_ES:
            {
                audio_renderer_looper_.append(std::make_shared<Message>([this] {
                            int feed_len = Feed_AudioData();
                            if (feed_len >= 0) return NDL_ESP_RESULT_SUCCESS;
                            else               return NDL_ESP_RESULT_FAIL;
                            }));
                break;
            }
        default:
            NDLLOG(LOGTAG, NDL_LOGE, "%s, cannot be here!!!", __func__);
            break;
    }
    //GETTIME(&endTime, NULL);
    //TIME_DIFF(startTime, endTime, elapse);

    //NDLLOG(LOGTAG, LOG_INOUT, "%s(type:%d)(elapsed:%dms) -", __FUNCTION__, buff->stream_type, elapse/1000);
    NDLLOG(LOGTAG, LOG_INOUT, "%s(type:%d) -", __func__, buff->stream_type);
    return buff->data_len;
}

int Esplayer::play()
{
    NDLASSERT(state_.canTransit(NDL_ESP_STATUS_PLAYING));
    NDLLOG(SDETTAG, LOG_INOUT, "%s +", __func__);
    if (!state_.canTransit(NDL_ESP_STATUS_PLAYING)) {
        NDLLOG(LOGTAG, NDL_LOGE, "unsupported operation, current state:%d", state_.get());
        return NDL_ESP_RESULT_FAIL;
    }
    int result = NDL_ESP_RESULT_SUCCESS;
    sync_state_ = ALLOW_VIDEO;

    if (state_.get() != NDL_ESP_STATUS_PAUSED)
        audio_last_pts_ = 0;

    do {
        if (state_.get() != NDL_ESP_STATUS_PLAYING)
            waiting_first_frame_presented_ = true;

        if (state_.get() == NDL_ESP_STATUS_LOADED) {
            result = NDL_ESP_RESULT_CLOCK_ERROR;
            BREAK_IF_NONZERO(clock_->setPlaybackRate(target_playback_rate_), "setting playback rate");

            result = NDL_ESP_RESULT_SET_STATE_ERROR;
            LOG_IF_NONZERO(video_codec_&&video_codec_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "change video_codec_ state to excuting");
            if( meta_.extrasize )
                sendVideoDecoderConfig();

            LOG_IF_NONZERO(video_scheduler_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "change video_scheduler_ state to excuting");
            LOG_IF_NONZERO(audio_codec_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "change audio_codec_ state to excuting");
            sendAudioDecoderConfig();
        }
        else if (state_.get() == NDL_ESP_STATUS_PAUSED) {
            result = NDL_ESP_RESULT_CLOCK_ERROR;
            BREAK_IF_NONZERO(clock_->setPlaybackRate(target_playback_rate_), "setting playback rate");

            result = NDL_ESP_RESULT_SET_STATE_ERROR;
            BREAK_IF_NONZERO(changeComponentsState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "set state to execute and wait done");
        }
        result = NDL_ESP_RESULT_SUCCESS;
    } while(0);

    state_.transit(NDL_ESP_STATUS_PLAYING);
    video_renderer_looper_.setRunningState(true);
    audio_renderer_looper_.setRunningState(true);
    video_message_looper_.setRunningState(true);
    audio_message_looper_.setRunningState(true);

    rm_->notifyActivity();
    rm_->disableScreenSaver();
    NDLLOG(LOGTAG, LOG_INOUT, "%s -", __func__);
    return result;
}

int Esplayer::pause()
{
    NDLLOG(SDETTAG, LOG_INOUT, "%s +", __func__);
    NDLASSERT(state_.canTransit(NDL_ESP_STATUS_PAUSED));

    if (!state_.canTransit(NDL_ESP_STATUS_PAUSED)) {
        NDLLOG(LOGTAG, NDL_LOGE, "unsupported operation, current state:%d", state_.get());
        return NDL_ESP_RESULT_FAIL;
    }
    if (state_.get()==NDL_ESP_STATUS_LOADED||state_.get()==NDL_ESP_STATUS_PAUSED){
        NDLLOG(SDETTAG, LOG_INOUT, "%s -", __func__);
        return NDL_ESP_RESULT_SUCCESS;
    }

    if (!loaded_) {
        NDLLOG(LOGTAG, NDL_LOGI, "pause called but already unloaded");
        return NDL_ESP_RESULT_FAIL;
    }

    video_renderer_looper_.setRunningState(false);
    audio_renderer_looper_.setRunningState(false);
    video_message_looper_.setRunningState(false);
    audio_message_looper_.setRunningState(false);

    std::lock_guard<std::mutex> lock(unload_mutex_);

    int result = NDL_ESP_RESULT_CLOCK_ERROR;
    do {
        BREAK_IF_NONZERO(clock_->setPlaybackRate(0), "setting clock paused");

        result = NDL_ESP_RESULT_SET_STATE_ERROR;

        BREAK_IF_NONZERO(changeComponentsState(OMX_StatePause, MAX_STATE_WAIT_TIME),
                "set state to pause and wait done");
        state_.transit(NDL_ESP_STATUS_PAUSED);
        NDLLOG(SDETTAG, LOG_INOUT, "%s -", __func__);
        NDLLOG(LOGTAG, NDL_LOGV, "decoded frame count : %u", frame_count_);
        frame_count_ = 0;
        result = NDL_ESP_RESULT_SUCCESS;
    } while(0);

    rm_->notifyActivity();
    rm_->enableScreenSaver();
    return result;
}

int Esplayer::stepFrame()
{
    NDLASSERT(state_.canTransit(NDL_ESP_STATUS_STEPPING));
    NDLLOG(SDETTAG, LOG_INOUT, "%s +", __func__);
    if (!state_.canTransit(NDL_ESP_STATUS_STEPPING)) {
        NDLLOG(LOGTAG, NDL_LOGE, "unsupported operation, current state:%d", state_.get());
        return NDL_ESP_RESULT_FAIL;
    }
    int result = NDL_ESP_RESULT_SUCCESS;

    NDL_ESP_STATUS save_state = state_.get();
    state_.transit(NDL_ESP_STATUS_STEPPING);

    result = clock_->stepFrame(PORT_CLOCK_VIDEO);

    // set player state to previous state of stepping
    state_.transit(save_state);
    NDLLOG(SDETTAG, LOG_INOUT, "%s -", __func__);
    return result;
}

int Esplayer::stepFrameNonTunnelMode()
{
    NDLASSERT(video_codec_ && video_renderer_);

    std::unique_lock<std::mutex> lock(stepping_mutex_);

    NDLASSERT(!stepping_);

    stepping_ = true;

    if (clock_)
        LOG_IF_NONZERO(clock_->setWaitingForStartTime(PORT_CLOCK_VIDEO), "setWaitingForStartTime");

    int ret = NDL_ESP_RESULT_SUCCESS;

    if (changeComponentsState(OMX_StateExecuting, MAX_STATE_WAIT_TIME) != NDL_ESP_RESULT_SUCCESS) {
        ret = NDL_ESP_RESULT_FAIL;
        NDLLOG(LOGTAG, NDL_LOGE, "%s : error in setting state to executing", __func__);
    }

    if (std::cv_status::timeout == stepping_cond_.wait_for(lock, std::chrono::seconds(1))) {
        ret = NDL_ESP_RESULT_FAIL;
        NDLLOG(LOGTAG, NDL_LOGE, "%s : error in waiting for stepping, timed out", __func__);
    }


    if (changeComponentsState(OMX_StatePause, MAX_STATE_WAIT_TIME) != NDL_ESP_RESULT_SUCCESS) {
        ret = NDL_ESP_RESULT_FAIL;
        NDLLOG(LOGTAG, NDL_LOGE, "%s : error in setting state to pause", __func__);
    }

    has_rendering_started_ = false;
    stepping_ = false;

    return ret;
}

int Esplayer::flush()
{
    NDLASSERT(state_.canTransit(NDL_ESP_STATUS_FLUSHING));
    NDLLOG(SDETTAG, LOG_INOUT, "%s (state : %d) +", __func__, state_.get());
    if (!state_.canTransit(NDL_ESP_STATUS_FLUSHING)) {
        NDLLOG(LOGTAG, NDL_LOGE, "unsupported operation, current state:%d", state_.get());
        return NDL_ESP_RESULT_FAIL;
    }
    int result = NDL_ESP_RESULT_FAIL;

    /*
    if (reconfiguring_) {
        std::unique_lock<std::mutex> lock(reconfiguring_mutex_);
        NDLLOG(LOGTAG, NDL_LOGI, "wait to finish audio port reconfiguration...");
        if (std::cv_status::timeout == reconfiguring_cond_.wait_for(lock, std::chrono::seconds(1))) {
            NDLLOG(LOGTAG, NDL_LOGE, "%s : error in waiting for re-configuring, timed out", __func__);
        }
        NDLLOG(LOGTAG, NDL_LOGI, "finish audio port reconfiguration...");
    }
    */

    if (!loaded_) {
        NDLLOG(LOGTAG, NDL_LOGI, "flush called but already unloaded");
        return NDL_ESP_RESULT_SUCCESS;
    }

    std::lock_guard<std::mutex> lock(unload_mutex_);

    trick_mode_enabled_ = false;

    audio_last_pts_ = 0;
    video_last_pts_ = 0;

    if (audio_renderer_) audio_eos_ = false;
    if (video_renderer_) video_eos_ = false;

    // save current player state to return back after flush finish
    NDL_ESP_STATUS save_state = state_.get();

    // set player state > flushing
    state_.transit(NDL_ESP_STATUS_FLUSHING);

    do {
        // set clock state > stopped
        if (clock_)
        {
            BREAK_IF_NONZERO(clock_->setStopped(), "set clock config : clock state -> stopped");
        }

        clearFrameQueues();

        clearBufQueue(NDL_ESP_AUDIO_ES);
        clearBufQueue(NDL_ESP_VIDEO_ES);

        //TODO need to consider the other platforms
        // set all the components state > paused
        if (save_state != NDL_ESP_STATUS_PAUSED)
        {
            result = NDL_ESP_RESULT_SET_STATE_ERROR;
            RETURN_IF_NONZERO(changeComponentsState(OMX_StatePause, MAX_STATE_WAIT_TIME), "set state to pause");
        }

        // flush all the components
        result = NDL_ESP_RESULT_FAIL;
        RETURN_IF_NONZERO(setComponentsFlush(MAX_FLUSH_WAIT_TIME), "flush components and wait done");

        // set clock state waitingForStartTime (for lip sync)
        if (clock_)
        {
            result = NDL_ESP_RESULT_CLOCK_ERROR;
                int port_mask = 0;
                if( enable_audio_ )
                    port_mask = 1;
                if( enable_video_ )
                    port_mask |= 2;
                BREAK_IF_NONZERO(clock_->setWaitingForStartTime(port_mask),
                        "setting clock state to waitingForStartTime");
            has_rendering_started_ = false;
        }

        // clear pts wraparound info
        for (auto& i : pts_base_)
            i = 0;
        for (auto& i : pts_previous_)
            i = -1;

        //TODO need to consider the other platforms
        if (save_state == NDL_ESP_STATUS_PLAYING)
        {
            // set all the components state > executing (SIC)
            result = NDL_ESP_RESULT_SET_STATE_ERROR;
            BREAK_IF_NONZERO(changeComponentsState(OMX_StateExecuting, 0),
                    "set state to executing");
        }
        result = NDL_ESP_RESULT_SUCCESS;

        // send the first frame presented event after flush
        waiting_first_frame_presented_ = true;

    } while(0);

    state_.transit(save_state);
    NDLLOG(SDETTAG, LOG_INOUT, "%s -", __func__);
    return result;
}

int Esplayer::getBufferLevel(NDL_ESP_STREAM_T type, uint32_t* level)
{
    if (type == NDL_ESP_AUDIO_ES && level && audio_codec_) {
        *level = audio_codec_->getUsedBufferCount(audio_codec_->getInputPortIndex());
        // NDLLOG(SDETTAG, NDL_LOGD, "audio buffer level : %d", *level);
        return NDL_ESP_RESULT_SUCCESS;
    } else if (type == NDL_ESP_VIDEO_ES && level && video_codec_) {
        *level = video_codec_->getUsedBufferCount(video_codec_->getInputPortIndex());
        //  NDLLOG(SDETTAG, NDL_LOGD, "video buffer level : %d", *level);
        return NDL_ESP_RESULT_SUCCESS;
    }
    return NDL_ESP_RESULT_FAIL;
}

int Esplayer::setPlaybackRate(int rate)
{
    //TODO consider -> without clock component
    NDLLOG(SDETTAG, NDL_LOGI, "set playback rate >> %d", rate);

    // supports speed 0x~2x
    if (rate<0 || rate>2000)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "playbak rate is too small or large.. (%d)", rate);
        return NDL_ESP_RESULT_FAIL;
    }

    target_playback_rate_ = rate;

    // set clock scale only if player state is playing or loaded
    if (clock_ && (state_.get() == NDL_ESP_STATUS_PLAYING ||
                state_.get() == NDL_ESP_STATUS_LOADED))
        clock_->setPlaybackRate(target_playback_rate_);

    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::setTrickMode(bool enable)
{
    NDLLOG(SDETTAG, LOG_INOUT, "%s(%d) +", __func__, enable);
    if (!trick_mode_enabled_ && !enable) { // ignore absolutely redundant case only
        NDLLOG(SDETTAG, LOG_INOUT, "%s(%d) ignored -", __func__, enable);
        return NDL_ESP_RESULT_SUCCESS;
    }

    trick_mode_enabled_ = enable;
    if (clock_)
        clock_->setTrickMode(enable);

    if (video_codec_) {

    }

    NDLLOG(SDETTAG, LOG_INOUT, "%s(%d) -", __func__, enable);
    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::setVolume(int volume, int duration, NDL_ESP_EASE_TYPE type)
{
    NDLLOG(SDETTAG, NDL_LOGI, "volume %d, ease duration %d & type %d", volume, duration, type);

    if (volume < 0 || volume > 100) {
        NDLLOG(LOGTAG, NDL_LOGE, "volume is too small or large.. (%d)", volume);
        return NDL_ESP_RESULT_FAIL;
    }
    return NDL_ESP_RESULT_SUCCESS;
}

int Esplayer::getMediaTime(int64_t* start_time, int64_t* current_time)
{
    if (clock_) {
        return clock_->getMediaTime(start_time, current_time);
    }
    return NDL_ESP_RESULT_FAIL;
}

int Esplayer::reloadAudio(NDL_ESP_META_DATA* meta)
{
    return NDL_ESP_RESULT_SUCCESS;
}

void Esplayer::onVideoInfoEvent(void* data)
{
    NDLLOG(LOGTAG, NDL_LOGE, "%s, video info event presented", __func__);
    memcpy(&videoInfo_, data, sizeof(videoInfo_));
    // Application has higher priority than soc's decision. sometimes ES doesn't have 3D type data.
    if (appForced3Dtype_ != E3DTYPE_NONE) {
        videoInfo_.E3DTYPE = appForced3Dtype_;
    }
    NDLLOG(LOGTAG, NDL_LOGI, "    video info %dx%d, %d/%d fps, scanType : %d, isHdr : %d",
            (int)videoInfo_.width, (int)videoInfo_.height,
            (int)videoInfo_.framerateNum, (int)videoInfo_.framerateDen,
            (int)videoInfo_.SCANTYPE, videoInfo_.hasHdrInfo);
    rm_->setVideoInfo(videoInfo_);
    notifyClient(NDL_ESP_VIDEO_INFO, (void*)&videoInfo_);
}

int Esplayer::onVideoCodecCallback(int event,
        uint32_t data1,
        uint32_t data2,
        void* data)
{
    NDLLOG(LOGTAG, LOG_FEEDINGV, "%s, event:%d, data1:%d, data2:%d, data:%p", __func__, event, data1, data2, data);

    switch (event) {
        case OMX_CLIENT_EVT_EMPTY_BUFFER_DONE:
            {
                int32_t used_buf_cnt = video_codec_->getUsedBufferCount(video_codec_->getInputPortIndex());
                int32_t free_buf_cnt = video_codec_->getFreeBufferCount(video_codec_->getInputPortIndex());
                NDL_ESP_STREAM_T stream_type = ((int)data1 == video_codec_->getInputPortIndex())? NDL_ESP_VIDEO_ES:NDL_ESP_AUDIO_ES;
                NDLLOG(LOGTAG, LOG_FEEDINGV, "VIDEO EMPTY BUFFER DONE(port:%u, buf_idx:%u)(u:%d/f:%d)(stream:%d)", data1, data2, used_buf_cnt, free_buf_cnt, stream_type);
                ++frame_count_;
                break;
            }
        case OMX_CLIENT_EVT_FILL_BUFFER_DONE:
            { // non-tunnel mode only
                break;
            }
        case OMX_CLIENT_EVT_PORT_SETTING_CHANGED:
            {
                NDLLOG(LOGTAG, NDL_LOGI, "VIDEOCODEC_PORT_SETTING_CHANGED plane_id:%d", plane_id_);

                LOG_IF_NONZERO(video_codec_&&video_codec_->enablePort(video_codec_->getOutputPortIndex(), false, 0),
                        "disable output port on video decoder");
                LOG_IF_NONZERO(video_scheduler_&&video_scheduler_->enablePort(video_scheduler_->getInputPortIndex(), false, 0),
                        "disable input port on video scheduler");

                OMX_PARAM_PORTDEFINITIONTYPE port_config;
                memset(&port_config, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
                port_config.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
                port_config.nVersion.nVersion = OMX_VERSION;
                port_config.nPortIndex = video_codec_->getOutputPortIndex();
                BREAK_IF_NONZERO(video_codec_->getParam(OMX_IndexParamPortDefinition, &port_config), "get parameter of video output port");

                memset(&videoInfo_, 0, sizeof(NDL_ESP_VIDEO_INFO_T));
                videoInfo_.width = port_config.format.video.nFrameWidth;
                videoInfo_.height = port_config.format.video.nFrameHeight;
                videoInfo_.SCANTYPE = SCANTYPE_PROGRESSIVE;
                videoInfo_.PARwidth = 1;
                videoInfo_.PARheight = 1;
                NDLLOG(LOGTAG, NDL_LOGI, "video info %dx%d", (int)videoInfo_.width, (int)videoInfo_.height);
                rm_->setVideoInfo(videoInfo_);

                NDLLOG(LOGTAG, NDL_LOGI, "Video DecoderConfig-> width = %d, height = %d, strid = %d, framerate %d, buffersize %u",
                        port_config.format.video.nFrameWidth, port_config.format.video.nFrameHeight, port_config.format.video.nStride,
                        port_config.format.video.xFramerate, port_config.nBufferSize);
                video_renderer_->setConfig(OMX_IndexConfigDisplayRegion, &port_config.format);

                LOG_IF_NONZERO(video_codec_->setupTunnel(video_codec_->getOutputPortIndex(),
                            video_scheduler_.get(), video_scheduler_->getInputPortIndex()),
                        "port setting changed - retunneling between decoder and scheduler");

                //set plane id
                //OMX_CONFIG_PLANEBLENDTYPE configPlane;
                //configPlane.nPortIndex = plane_id_;
                //LOG_IF_NONZERO(video_renderer_->setConfig(OMX_IndexConfigCommonPlaneBlend, &configPlane), "OMX_IndexConfigCommonPlaneBlend");

                // WORK-AROUND: to remove green-screen at the beginning, let the renderer execute after the 1st port setting change
                // if rm->mediaContentReady() works well, this code would not be needed
                if (is_first_port_setting_change_) {
                    LOG_IF_NONZERO(video_renderer_&&video_renderer_->setState(OMX_StateExecuting, 1),
                        "change video_renderer_ state to excuting");
                    is_first_port_setting_change_ = false;
                }

                if( sync_state_ != ALLOW_VIDEO ){
                    NDLLOG(LOGTAG, NDL_LOGI, "before notifyClient about port_changed, set threshold_sync_state to ALLOW_VIDEO");
                    sync_state_ = ALLOW_VIDEO;
                }

                video_message_looper_.append(std::make_shared<Message>([this]{
                            NDLLOG(LOGTAG, NDL_LOGI, "notifyClient NDL_ESP_VIDEO_PORT_CHANGED");
                            notifyClient(NDL_ESP_VIDEO_PORT_CHANGED);
                            return NDL_ESP_RESULT_SUCCESS;
                            }));
                break;
            }
        case OMX_CLIENT_EVT_END_OF_STREAM:
            {
                NDLLOG(SDETTAG, NDL_LOGI, "%s, OMX_CLIENT_EVT_END_OF_STREAM", __func__);
                video_eos_ = true;
                if (!audio_renderer_ || audio_eos_) {
                    NDLLOG(SDETTAG, NDL_LOGI, "notifyClient NDL_ESP_END_OF_STREAM");
                    video_message_looper_.post(std::make_shared<Message>([this]{
                          notifyClient(NDL_ESP_END_OF_STREAM);
                          return NDL_ESP_RESULT_SUCCESS;
                          }));
                    rm_->endOfStream();
                }

                break;
            }
        case Component::EVENT_VIDEO_INFO:
            onVideoInfoEvent(data);
            break;

        default:
            NDLLOG(LOGTAG, NDL_LOGI, "%s, unknown event:%d(0x%x), data1:%d, data2:%d",
                    __func__, event, event, data1, data2);
            break;
    }
    return OMX_ErrorNone;
}

#ifdef OMX_NONE_TUNNEL
void Esplayer::onVideoRenderEvent(int64_t timestamp)
{
    ++frame_count_;
    if (waiting_first_frame_presented_) {
        NDLLOG(LOGTAG, NDL_LOGI, "%s, first frame presented", __func__);

        waiting_first_frame_presented_ = false;
        rm_->mediaContentReady(true);
        video_message_looper_.post(std::make_shared<Message>([this]{
                    NDLLOG(LOGTAG, NDL_LOGI, "notifyClient NDL_ESP_FIRST_FRAME_PRESENTED");
                    notifyClient(NDL_ESP_FIRST_FRAME_PRESENTED);
                    return NDL_ESP_RESULT_SUCCESS;
                    }));
    } else {
        //FIXME : Need to consider timestamp rollover
        NDLLOG(LOGTAG, LOG_FEEDINGV, "video render done >> pts: %lld", timestamp);
    }
}

int Esplayer::onVideoSchedulerCallback(int event,
        uint32_t data1,
        uint32_t data2,
        void* data)
{

    NDLLOG(LOGTAG, NDL_LOGD, "%s event:0x%x, data1:0x%x, data2:0x%x", __func__, event, data1, data2);
    return OMX_ErrorNone;
}

int Esplayer::onVideoRendererCallback(int event,
        uint32_t data1,
        uint32_t data2,
        void* data)
{
    NDLLOG(LOGTAG, NDL_LOGV, "%s: event %d, data1 %u, data2 %u", __func__, event, data1, data2);
    int free_buffer_cnt;
    int input_buffer_cnt;

    switch (event) {
        case OMX_CLIENT_EVT_EMPTY_BUFFER_DONE:
            { // non-tunnel mode only

                NDLLOG(LOGTAG, NDL_LOGE, "%s: OMX_CLIENT_EVT_EMPTY_BUFFER_DONE", __func__);

                if (stepping_) {
                    NDLLOG(LOGTAG, NDL_LOGI, "%s: signal stepping...on empty-buffer-done", __func__);
                    stepping_cond_.notify_one();
                }
                OMX_BUFFERHEADERTYPE* buf = video_renderer_->getBuffer(data1, data2);

                NDLLOG(LOGTAG, NDL_LOGE, "onVideoRendererCallback %25s pts:%11lld buffer:%d", __func__, buf->nTimeStamp, data2);
                if (video_renderer_ && video_codec_) {
                    if (video_codec_->fillBuffer(video_codec_->getOutputPortIndex(), data2) != 0) {
                        NDLLOG(LOGTAG, NDL_LOGE, "%s: error in fillBuffer", __func__);
                    }
                }

                onVideoRenderEvent(from_omx_time(buf->nTimeStamp));
                break;
            }

        case OMX_CLIENT_EVT_FILL_BUFFER_DONE:
            break;

        case OMX_CLIENT_EVT_PORT_SETTING_CHANGED:
            break;

        case Component::EVENT_VIDEO_INFO:
            onVideoInfoEvent(data);
            break;

        case Component::EVENT_RENDER: // SIC tunnel-mode only
            if (stepping_) {
                NDLLOG(LOGTAG, NDL_LOGI, "%s: signal stepping...on custom render event", __func__);
                stepping_cond_.notify_one();
            }
            if (data)
                onVideoRenderEvent(*(int64_t*)data);
            else
                onVideoRenderEvent();
            break;

        case OMX_CLIENT_EVT_END_OF_STREAM:
            NDLLOG(SDETTAG, NDL_LOGI, "%s, OMX_CLIENT_EVT_END_OF_STREAM", __func__);
            video_eos_ = true;
            if (!audio_renderer_ || audio_eos_) {
                NDLLOG(SDETTAG, NDL_LOGI, "notifyClient NDL_ESP_END_OF_STREAM");
                video_message_looper_.post(std::make_shared<Message>([this]{
                            notifyClient(NDL_ESP_END_OF_STREAM);
                            return NDL_ESP_RESULT_SUCCESS;
                            }));
                rm_->endOfStream();
            }
            break;

        case OMX_CLIENT_EVT_UNDERFLOW:
            free_buffer_cnt = video_codec_->getFreeBufferCount(video_codec_->getInputPortIndex());
            input_buffer_cnt = video_codec_->getInputBufferCount();
            if ((input_buffer_cnt - free_buffer_cnt) >= VIDEO_IN_BUFFER_COUNT_LOW)
                break;

            sync_state_ = ALLOW_VIDEO;
            NDLLOG(SDETTAG, NDL_LOGI, "notifyClient NDL_ESP_STREAM_DRAINED from video");
            notifyClient(NDL_ESP_STREAM_DRAINED_VIDEO);
            break;

        case OMX_EventVendorStartUnused: // render event from OMXdrm
            if (data)
                onVideoRenderEvent(*(int64_t*)data);
            else
                onVideoRenderEvent();
            break;

        default:
            NDLLOG(LOGTAG, NDL_LOGI, "%s, unknown event:%d(0x%x), data1:%d, data2:%d",
                    __func__, event, event, data1, data2);
            break;
    }
    return OMX_ErrorNone;
}
#endif

int Esplayer::onAudioCodecCallback(int event,
        uint32_t data1,
        uint32_t data2,
        void* data)
{
    NDLLOG(LOGTAG, NDL_LOGV, "%s event:%d", __func__, event);

    switch (event) {
        case OMX_CLIENT_EVT_RESOURCE_ACQUIRED:
            NDLLOG( LOGTAG, NDL_LOGD, "OMX_CLIENT_EVT_RESOURCE_ACQUIRED");
            LOG_IF_NONZERO(audio_codec_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "change audio_codec_ state to excuting");
            sendAudioDecoderConfig();
            break;

        case OMX_CLIENT_EVT_EMPTY_BUFFER_DONE:
            {
                NDL_ESP_STREAM_T stream_type = ((int)data1 == audio_codec_->getInputPortIndex())? NDL_ESP_AUDIO_ES:NDL_ESP_VIDEO_ES;
                NDLLOG(LOGTAG, LOG_FEEDINGV, "AUDIO EMPTY BUFFER DONE (port:%u, buf_idx:%u)(stream:%d)", data1, data2, stream_type);
                break;
            }
        case OMX_CLIENT_EVT_FILL_BUFFER_DONE:
            { // non-tunnel mode only
                break;
            }
        case OMX_CLIENT_EVT_PORT_SETTING_CHANGED:
            NDLLOG(LOGTAG, NDL_LOGI, "audio decoder port:%u settings changed", data1);
            NDLASSERT(data1 == (uint32_t)audio_codec_->getOutputPortIndex());
            /*
            reconfiguring_ = true;

            audio_message_looper_.post(std::make_shared<Message>([this]{
                        NDLLOG(LOGTAG, NDL_LOGD, "audio decoder port setting changed detected");
                        return onAudioCodecDetected();
                        }));
            */
            audio_message_looper_.post(std::make_shared<Message>([this]{
                        NDLLOG(LOGTAG, NDL_LOGD, "notifyClient NDL_ESP_AUDIO_PORT_CHANGED");
                        notifyClient(NDL_ESP_AUDIO_PORT_CHANGED);
                        return NDL_ESP_RESULT_SUCCESS;
                        }));
            break;
        case OMX_CLIENT_EVT_END_OF_STREAM:
            NDLLOG(SDETTAG, NDL_LOGI, "%s, OMX_CLIENT_EVT_END_OF_STREAM", __func__);
            audio_eos_ = true;
            if (1/*!video_renderer_ || video_eos_*/) {
                NDLLOG(SDETTAG, NDL_LOGI, "notifyClient NDL_ESP_END_OF_STREAM");
                audio_message_looper_.post(std::make_shared<Message>([this]{
                            notifyClient(NDL_ESP_END_OF_STREAM);
                            return NDL_ESP_RESULT_SUCCESS;
                            }));
                rm_->endOfStream();
            }
            break;
        default:
            NDLLOG(LOGTAG, NDL_LOGI, "%s, unknown event:%d(0x%x), data1:%d, data2:%d",
                    __func__, event, event, data1, data2);
            break;
    }
    return OMX_ErrorNone;
}

int Esplayer::onAudioCodecDetected()
{
    NDLLOG(LOGTAG, NDL_LOGD, "onAudioCodecDetected!");
    // prevent to unload during buffer re-allocation
    std::lock_guard<std::mutex> lock(unload_mutex_);
    if (!loaded_) {
        NDLLOG(LOGTAG, NDL_LOGI, "onAudioCodecDetected called but already unloaded");
        return NDL_ESP_RESULT_SUCCESS;
    }

    do {

        NDLLOG(LOGTAG, NDL_LOGI, "%s: signal reconfiguring...", __func__);
        reconfiguring_ = false;
        reconfiguring_cond_.notify_one();

        return OMX_ErrorNone;
    } while(0);

    NDLLOG(LOGTAG, NDL_LOGI, "%s: signal reconfiguring...", __func__);
    reconfiguring_ = false;
    reconfiguring_cond_.notify_one();
    return NDL_ESP_RESULT_FAIL;
}

#if SUPPORT_AUDIOMIXER
#ifdef OMX_NONE_TUNNEL
int Esplayer::onAudioMixerCallback(int event,
        uint32_t data1,
        uint32_t data2,
        void* data)
{
    NDLLOG(LOGTAG, NDL_LOGD, "%s event:0x%x, data1:0x%x, data2:0x%x", __func__, event, data1, data2);
    return OMX_ErrorNone;
}
#endif
#endif

#ifdef OMX_NONE_TUNNEL
void Esplayer::onAudioRenderEvent(int64_t timestamp)
{
    if (enable_audio_ && !enable_video_ && waiting_first_frame_presented_) {
        waiting_first_frame_presented_ = false;
    } else {
        //FIXME : Need to consider timestamp rollover
        NDLLOG(LOGTAG, LOG_FEEDINGV, "audio render done >> %lld", timestamp);
    }
}

int Esplayer::onAudioRendererCallback(int event,
        uint32_t data1,
        uint32_t data2,
        void* data)
{
    NDLLOG(LOGTAG, NDL_LOGV, "%s", __func__);

    switch (event) {
        case OMX_CLIENT_EVT_RESOURCE_ACQUIRED:
            NDLLOG( LOGTAG, NDL_LOGD, "OMX_CLIENT_EVT_RESOURCE_ACQUIRED");
            LOG_IF_NONZERO(audio_renderer_->setState(OMX_StateExecuting, MAX_STATE_WAIT_TIME),
                    "change audio_renderer_ state to excuting");
            break;
        case OMX_CLIENT_EVT_EMPTY_BUFFER_DONE:
            {// non-tunnel mode and pcm play only
                if (target_playback_rate_ == Clock::NORMAL_PLAYBACK_RATE
                        && audio_renderer_->getUsedBufferCount(data1) == 0
                        && (!audio_codec_ || audio_codec_->getUsedBufferCount(audio_codec_->getInputPortIndex()) == 0)) {
                    NDLLOG(SDETTAG, NDL_LOGI, "notifyClient NDL_ESP_STREAM_DRAINED from audio");
                    notifyClient(NDL_ESP_STREAM_DRAINED_AUDIO);
                }
                OMX_BUFFERHEADERTYPE* buf = audio_renderer_->getBuffer(data1, data2);
                NDLLOG(LOGTAG, NDL_LOGE, "%25s pts:%11lld buffer:%d", __func__, buf->nTimeStamp, data2);
                //   clock_->updateMediaTime(PORT_CLOCK_AUDIO, buf->nTimeStamp);

                if (audio_renderer_ && audio_codec_) {
                    if (audio_codec_->fillBuffer(audio_codec_->getOutputPortIndex(), data2) != 0) {
                        NDLLOG(LOGTAG, NDL_LOGE, "%s: error in fillBuffer", __func__);
                    }
                }
                else
                    if (audio_renderer_){
                        // NDLLOG(LOGTAG, NDL_LOGE, "%s: free_buffer_cnt : %d , input_buffer_cnt : %d used_buffer_cnt:%d", __func__, free_buffer_cnt, input_buffer_cnt, used_buffer_cnt);
                        int used_buffer_cnt = audio_codec_->getUsedBufferCount(audio_codec_->getInputPortIndex());
                        if ( used_buffer_cnt < AUDIO_IN_BUFFER_COUNT_LOW ) {
                            // handle it in a different thread
                            audio_message_looper_.post(std::make_shared<Message>([this]{
                                        //NDLLOG(LOGTAG, NDL_LOGD, "notifyClient NDL_ESP_LOW_THRESHOLD_CROSSED_AUDIO");
                                        notifyClient(NDL_ESP_LOW_THRESHOLD_CROSSED_AUDIO);
                                        return NDL_ESP_RESULT_SUCCESS;
                                        }));
                        }
                    }
                    else
                        NDLLOG(LOGTAG, NDL_LOGI, "%s: 0x%x event without audio renderer. Cannot be here.",
                                __func__, OMX_CLIENT_EVT_EMPTY_BUFFER_DONE);
                // onAudioRenderEvent(buf->nTimeStamp);
                //#endif
                break;
            }
        case OMX_CLIENT_EVT_FILL_BUFFER_DONE:
            break;
        case OMX_CLIENT_EVT_PORT_SETTING_CHANGED:
            NDLLOG(LOGTAG, NDL_LOGI, "audio renderer port %u settings changed.\n", data1);
            break;
        case Component::EVENT_RENDER: // SIC tunnel mode only
            NDLLOG(LOGTAG, NDL_LOGI, "%s, render event.", __func__);
            if (data)
                onAudioRenderEvent(*(int64_t*)data);
            else
                onAudioRenderEvent();
            break;
        case OMX_CLIENT_EVT_END_OF_STREAM:
            NDLLOG(SDETTAG, NDL_LOGI, "%s, OMX_CLIENT_EVT_END_OF_STREAM", __func__);
            audio_eos_ = true;
            if (!video_renderer_ || video_eos_) {
                NDLLOG(SDETTAG, NDL_LOGI, "notifyClient NDL_ESP_END_OF_STREAM");
                audio_message_looper_.post(std::make_shared<Message>([this]{
                            notifyClient(NDL_ESP_END_OF_STREAM);
                            return NDL_ESP_RESULT_SUCCESS;
                            }));
                rm_->endOfStream();
            }
            break;
        case OMX_CLIENT_EVT_UNDERFLOW:
            NDLLOG(SDETTAG, NDL_LOGI, "notifyClient NDL_ESP_STREAM_DRAINED from audio");
            notifyClient(NDL_ESP_STREAM_DRAINED_AUDIO);
            break;
        default:
            NDLLOG(LOGTAG, NDL_LOGI, "%s, unknown event:%d(0x%x), data1:%d, data2:%d",
                    __func__, event, event, data1, data2);
            break;
    }
    return OMX_ErrorNone;
}
#endif
void Esplayer::clearFrameQueues()
{
    video_renderer_looper_.clearAll();
    audio_renderer_looper_.clearAll();
}
int Esplayer::set3DType(const NDL_ESP_3D_TYPE e3DType)
{
    if (state_.get() >= NDL_ESP_STATUS_PLAYING) {
        return NDL_ESP_RESULT_FAIL;
    }

    appForced3Dtype_ = e3DType;
    return NDL_ESP_RESULT_SUCCESS;
}

void Esplayer::pushBufQueue(NDL_EsplayerBuffer buf)
{
    std::lock_guard<std::mutex> lock(frame_queue_mutex_[buf->stream_type]);
    stream_buff_queue_[buf->stream_type].push(buf);
}

void Esplayer::popBufQueue(const NDL_ESP_STREAM_T& stream_type)
{
    std::lock_guard<std::mutex> lock(frame_queue_mutex_[stream_type]);
    if( !stream_buff_queue_[stream_type].empty() )
        stream_buff_queue_[stream_type].pop();
}

NDL_ESP_STREAM_BUFFER* Esplayer::getBufQueue(const NDL_ESP_STREAM_T& stream_type)
{
    std::lock_guard<std::mutex> lock(frame_queue_mutex_[stream_type]);
    if( stream_buff_queue_[stream_type].empty() ) {

        NDLLOG(LOGTAG, NDL_LOGE, "%s, type:%d buffer empty", __func__, stream_type );
        return nullptr;
    }
    else {
        auto buf = stream_buff_queue_[stream_type].front();
        return buf.get();
    }
}

void Esplayer::clearBufQueue(NDL_ESP_STREAM_T stream_type)
{
    std::lock_guard<std::mutex> lock(frame_queue_mutex_[stream_type]);
    if (stream_buff_queue_[stream_type].empty())
        return;

    NDLLOG(LOGTAG, LOG_FEEDINGV, "%s, stream_buff_queue_[%d] %d items remaining !", __func__, stream_type, stream_buff_queue_[stream_type].size());
    while(!stream_buff_queue_[stream_type].empty()) {
        auto buff = stream_buff_queue_[stream_type].front();
        //NDLLOG(LOGTAG, LOG_FEEDINGV, "Free remaining data. timestamp:%lld, use_count:%d", buff.get()->timestamp, buff.use_count());
        stream_buff_queue_[stream_type].pop();
    }
}

int Esplayer::setVideoDisplayWindow(const long left, const long top,
        const long width, const long height,
        const bool isFullScreen) const
{
    NDLLOG(LOGTAG, NDL_LOGI, "%s left:%d top:%d width:%d height:%d isFullScreen:%d",
            __FUNCTION__, left, top, width, height, isFullScreen);

    return rm_->setVideoDisplayWindow(left, top, width, height, isFullScreen);
}

int Esplayer::setVideoCustomDisplayWindow(const long src_left, const long src_top,
        const long src_width, const long src_height,
        const long dst_left, const long dst_top,
        const long dst_width, const long dst_height,
        const bool isFullScreen) const
{
    NDLLOG(LOGTAG, NDL_LOGI, "%s src(left:%d top:%d width:%d height:%d) \
            dst(left:%d top:%d width:%d height:%d isFullScreen:%d)", __FUNCTION__,
            src_left, src_top, src_width, src_height,
            dst_left, dst_top, dst_width, dst_height, isFullScreen);

    return rm_->setVideoCustomDisplayWindow(src_left, src_top, src_width, src_height,
            dst_left, dst_top, dst_width, dst_height, isFullScreen);
}

// mute controlled by AV block of TV service
int Esplayer::muteAudio(bool mute) {
    return rm_->muteAudio(mute);
}

int Esplayer::muteVideo(bool mute) {
    return rm_->muteVideo(mute);
}

void Esplayer::printMetaData(const NDL_ESP_META_DATA* meta) const
{
    NDLLOG(LOGTAG, NDL_LOGI, "---------------Video and Audio information--------------");
    NDLLOG(LOGTAG, NDL_LOGI, "video_codec : %d, audio_codec : %d",
            meta->video_codec, meta->audio_codec);
    NDLLOG(LOGTAG, NDL_LOGI, "width : %d, height : %d, framerate : %d, video_encoding : %d, extradata : %p, extrasize : %d",
            meta->width, meta->height, meta->framerate,
            meta->video_encoding, meta->extradata, meta->extrasize);
    NDLLOG(LOGTAG, NDL_LOGI, "channels : %d, samplerate : %d, blockalign : %d, bitrate : %d, bitspersample : %d",
            meta->channels, meta->samplerate,
            meta->blockalign, meta->bitrate, meta->bitspersample);
    NDLLOG(LOGTAG, NDL_LOGI, "--------------------------------------------------------");
}

void Esplayer::printComponentInfo() const
{
    if(enable_video_) {
        NDLLOG(LOGTAG, NDL_LOGI, "--------------------Video components--------------------");
        if (video_codec_)     video_codec_->printComponentInfo();
        if (video_scheduler_) video_scheduler_->printComponentInfo();
        if (video_renderer_)  video_renderer_->printComponentInfo();
    }

    if(enable_audio_){
    NDLLOG(LOGTAG, NDL_LOGI, "--------------------Audio components--------------------");
    if (audio_codec_)    audio_codec_->printComponentInfo();
#if SUPPORT_AUDIOMIXER
    if (audio_mixer_)    audio_mixer_->printComponentInfo();
#endif
    if (audio_renderer_) audio_renderer_->printComponentInfo();
    NDLLOG(LOGTAG, NDL_LOGI, "--------------------------------------------------------");
    }
}

void Esplayer::printStreamBuf(const char* prefix_buf, NDL_ESP_STREAM_BUFFER* buf)
{
    NDLLOG(LOGTAG, LOG_FEEDINGV, "%s buf(%p) - data:%p, data_len:%d, type:%d, timestamp:%lld, flags:%d",
            prefix_buf, buf, buf->data, buf->data_len, buf->stream_type, buf->timestamp, buf->flags);

    if (buf->flags & NDL_ESP_FLAG_END_OF_STREAM)
        NDLLOG(LOGTAG, LOG_FEEDINGV, "%s Got EOS data NDL_ESP_FLAG_END_OF_STREAM", prefix_buf);

    if (buf->timestamp == 0)
        NDLLOG(LOGTAG, NDL_LOGD, "%s type:%d, starttime pts got", prefix_buf, buf->stream_type);

}
