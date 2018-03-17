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

#include <assert.h>
#include <climits>

#include "omxclock.h"

#define OMX_PRE_ROLL 200

#define LOGTAG "clock"
#include "debug.h"

using namespace NDL_Esplayer;


enum OMX_SETTINGS {
    PORT_CLOCK_OUTPUT = 81, //3

    CLOCK_BUFFER_COUNT = 2,
    CLOCK_OUT_BUFFER_SIZE = 0,
};

OmxClock::OmxClock()
{
}

OmxClock::~OmxClock()
{
    destroy();
}


void OmxClock::destroy()
{
    if(clock_)
        clock_->destroy();
    clock_ = nullptr;
}

int OmxClock::create()
{
    int result = NDL_ESP_RESULT_SUCCESS;

    clock_ = std::make_shared<Component>(
            [this] (int event, uint32_t data1, uint32_t data2, void* data){
#ifdef OMX_NONE_TUNNEL
            onCallback(event, data1, data2, data);
#endif
            });

    if(clock_->create(Component::CLOCK) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "error in creating clock");
        return NDL_ESP_RESULT_CLOCK_ERROR;
    }
    return result;
}

int OmxClock::reconfigureOutputBuffers()
{
    int result = NDL_ESP_RESULT_FAIL;
    do {
        BREAK_IF_NONZERO(clock_->enablePort(PORT_CLOCK_OUTPUT, true, 0),
                "enable clock output port");
        BREAK_IF_NONZERO(clock_->reconfigureBuffers(PORT_CLOCK_OUTPUT,
                    CLOCK_BUFFER_COUNT,
                    CLOCK_OUT_BUFFER_SIZE),
                "reconfiguring clock output buffer");
        result = NDL_ESP_RESULT_SUCCESS;
    } while(0);

    return result;
}

int OmxClock::getPortDefinition(int port_index)
{
    return clock_->getPortDefinition(port_index);
}

int OmxClock::enablePort(int port_index, bool enable, int timeout_seconds)
{
    int ret = clock_->enablePort(port_index, enable, timeout_seconds);
    return ret;
}

int OmxClock::connectComponent(int port_index,
        std::shared_ptr<Component> component,
        int component_port_index)
{
    return clock_->setupTunnel(port_index,
            component.get(),
            component_port_index);
}

int OmxClock::allocateOutputBuffer()
{
    int result = NDL_ESP_RESULT_CLOCK_BUFFER_ERROR;
    do {
        BREAK_IF_NONZERO(clock_->allocateBuffer(PORT_CLOCK_OUTPUT,
                    CLOCK_BUFFER_COUNT,
                    static_cast<int>(sizeof(OMX_TIME_MEDIATIMETYPE))),
                "allocating clock buffers");

        int i=0;
        for(i=0; i< CLOCK_BUFFER_COUNT; ++i) {
            BREAK_IF_NONZERO(clock_->fillBuffer(PORT_CLOCK_OUTPUT, i),
                    "call fillThisBuffer on clock output");
        }

        if(i == CLOCK_BUFFER_COUNT)
            result = NDL_ESP_RESULT_SUCCESS;

    } while(0);

    return result;
}

int OmxClock::freeOutputBuffer()
{
    return clock_->freeBuffer(PORT_CLOCK_OUTPUT);
}

int OmxClock::setPlaybackRate(int rate)
{
    target_clock_scale = (0x10000*rate)/Clock::NORMAL_PLAYBACK_RATE;
    return setScaleImpl(target_clock_scale);
}

int OmxClock::setState(OMX_STATETYPE state, int timeout_seconds)
{
    return clock_->setState(state, timeout_seconds);
}

int OmxClock::waitForState(OMX_STATETYPE state, int timeout_seconnds)
{
    return clock_->waitForState(state, timeout_seconnds);
}

int OmxClock::setScaleImpl(int scale)
{
    if(!clock_)
        return NDL_ESP_RESULT_FAIL;
    int result = NDL_ESP_RESULT_SUCCESS;
    do {
        OMX_TIME_CONFIG_SCALETYPE clock_scale;

        NDLLOG(LOGTAG, NDL_LOGD, "%s(scale:%d), clock state:", __func__, scale, getOmxClockState());

        memset (&clock_scale, 0, sizeof (clock_scale));
        clock_scale.nSize = sizeof (clock_scale);
        clock_scale.nVersion.nVersion = OMX_VERSION;
        clock_scale.xScale = scale;
        result = NDL_ESP_RESULT_CLOCK_STATE_ERROR;
        BREAK_IF_NONZERO( clock_->setConfig(OMX_IndexConfigTimeScale, &clock_scale), "set clock scale");

        result = NDL_ESP_RESULT_SUCCESS;
    } while(0);

    return result;
}

int OmxClock::getOmxClockState()
{
    if (!clock_)
        return NDL_ESP_RESULT_FAIL;

    OMX_TIME_CONFIG_CLOCKSTATETYPE clock_state;
    omx_init_structure(&clock_state, OMX_TIME_CONFIG_CLOCKSTATETYPE);

    // get clock state
    int ret = clock_->getConfig (OMX_IndexConfigTimeClockState, &clock_state);
    if (ret == OMX_ErrorNone) {
        return (int)clock_state.eState;
    }

    return NDL_ESP_RESULT_FAIL;
}

int OmxClock::setWaitingForStartTime(int port_index)
{
    if(!clock_)
        return NDL_ESP_RESULT_FAIL;

    OMX_TIME_CONFIG_CLOCKSTATETYPE clock_state;

    OMXSetReferenceClock(true,false); //true

    // get clock state
    clock_->getConfig (OMX_IndexConfigTimeClockState, &clock_state);
    NDLLOG(LOGTAG, NDL_LOGV, "%s : clock state:%d, port_index:%d", __func__, clock_state.eState, port_index);

    if (setStopped() < 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "%s : setStopped failed", __func__);
        return NDL_ESP_RESULT_FAIL;
    }

    // do not set clock waitingForStartTime if clock is running
    clock_state.nSize = sizeof (clock_state);
    clock_state.nVersion.nVersion = OMX_VERSION;
    clock_state.eState = OMX_TIME_ClockStateWaitingForStartTime;
    clock_state.nStartTime = to_omx_time(0); //LLONG_MAX;
    clock_state.nOffset = to_omx_time(-1000LL * OMX_PRE_ROLL);// 4294767296;//-200000; 0;
    clock_state.nWaitMask = port_index; /* waitMask = 1 (audio only), waitMast = 2 (video only), waitMask = 3 (audio and video) */
    NDLLOG(LOGTAG, NDL_LOGD, "%s : clock_state.nWaitMask=%d", __func__,clock_state.nWaitMask );
    return clock_->setConfig (OMX_IndexConfigTimeClockState, &clock_state);
}

int OmxClock::setStopped()
{
    if(!clock_)
        return NDL_ESP_RESULT_FAIL;

    do {
        OMX_TIME_CONFIG_CLOCKSTATETYPE clock_state;
        omx_init_structure(&clock_state, OMX_TIME_CONFIG_CLOCKSTATETYPE);
        BREAK_IF_NONZERO(
                clock_->getConfig(OMX_IndexConfigTimeClockState, &clock_state),
                "get clock config");

        if (clock_state.eState != OMX_TIME_ClockStateStopped)
        {
            clock_state.eState = OMX_TIME_ClockStateStopped;
            BREAK_IF_NONZERO(
                    clock_->setConfig(OMX_IndexConfigTimeClockState, &clock_state),
                    "set clock config : clock state -> stopped");
        }
        return NDL_ESP_RESULT_SUCCESS;
    } while(0);

    return NDL_ESP_RESULT_CLOCK_STATE_ERROR;
}

int OmxClock::getRealTime(int64_t* start_time, int64_t* current_time)
{
    NDLASSERT(0 && "not implemented !!!");
    return -1;
}

//Not support in RPI
int OmxClock::getMediaTime(int64_t* start_time, int64_t* current_time)
{
    return -1;
}

int OmxClock::onCallback(int event,
        uint32_t data1,
        uint32_t data2,
        void* data)
{
    NDLLOG(LOGTAG, NDL_LOGV, "%s", __func__);

    switch(event) {
        case OMX_CLIENT_EVT_EMPTY_BUFFER_DONE:
            break;
        case OMX_CLIENT_EVT_FILL_BUFFER_DONE: {
                                                  OMX_BUFFERHEADERTYPE* buf = clock_->getBuffer(data1, data2);
                                                  OMX_TIME_MEDIATIMETYPE* mt = (OMX_TIME_MEDIATIMETYPE*)buf->pBuffer;

                                                  if (buf->nFilledLen != sizeof(*mt)) {
                                                      NDLLOG(LOGTAG, NDL_LOGI, "clock buffer returned... (%d) ", (int)buf->nFilledLen);
                                                      return OMX_ErrorNone;
                                                  }
                                                  if (mt->eState == OMX_TIME_ClockStateRunning) {
                                                      if (mt->eUpdateType == OMX_TIME_UpdateClockStateChanged && mt->xScale != target_clock_scale) {
                                                          setScaleImpl(target_clock_scale);
                                                      }
                                                  }
                                                  clock_->fillBuffer(data1, data2);
                                                  break;
                                              }
        case OMX_CLIENT_EVT_PORT_SETTING_CHANGED:
                                              break;
        default:
                                              NDLLOG(LOGTAG, NDL_LOGI, "%s, unknown event:%d, data1:%d, data2:%d", __func__, event, data1, data2);
                                              break;
    }
    return OMX_ErrorNone;
}

//Not support in RPI
int OmxClock::stepFrame(uint32_t video_port_index)
{
    int result = NDL_ESP_RESULT_SUCCESS;
    return result;
}

bool OmxClock::OMXSetReferenceClock(bool has_audio, bool lock /* = true */)
{
    bool ret = true;
    int omx_err = 0; //OMX_ErrorNone;
    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
    omx_init_structure(&refClock, OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);

    if(has_audio)
        refClock.eClock = OMX_TIME_RefClockAudio;
    else
        refClock.eClock = OMX_TIME_RefClockVideo;

    refClock.nVersion.nVersion = OMX_VERSION;

    if (refClock.eClock != eClock)
    {
        NDLLOG(LOGTAG, NDL_LOGD, "OMXClock using %s as reference", refClock.eClock == OMX_TIME_RefClockVideo ? "video" : "audio");
        omx_err =  clock_->setConfig(OMX_IndexConfigTimeActiveRefClock, &refClock);
        if(omx_err != 0/*OMX_ErrorNone*/)
        {
            NDLLOG(LOGTAG, NDL_LOGE, "OMXSetReferenceClock: omx_err : %d",omx_err);
            ret = false;
        }
        eClock = refClock.eClock;
    }
    last_media_time = 0.0f;
    NDLLOG(LOGTAG, NDL_LOGD, "OMXSetReferenceClock: ret : %d",ret);
    return ret;
}
