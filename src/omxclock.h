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

#ifndef NDL_DIRECTMEDIA2_OMXCLOCK_H_
#define NDL_DIRECTMEDIA2_OMXCLOCK_H_



#include "component.h"
#include "clock.h"

namespace NDL_Esplayer {


    class OmxClock : public Clock {
        public:
            OmxClock();
            virtual ~OmxClock();

            int create() override;
            void destroy() override;

            int reconfigureOutputBuffers() override;

            int enablePort(int port_index, bool enable, int timeout_seconds) override;

            int connectComponent(int port_index,
                    std::shared_ptr<Component> component,
                    int component_port_index) override;

            int allocateOutputBuffer() override;
            int freeOutputBuffer() override;

            int stepFrame(uint32_t video_port_index) override;
            int setPlaybackRate(int rate) override;
            int setWaitingForStartTime(int port_index) override;
            int setStopped() override;
            void setTrickMode(bool enable) override { trick_mode_eanbled_ = enable;}

            int setState(OMX_STATETYPE state, int timeout_seconds) override;
            int waitForState(OMX_STATETYPE state, int timeout_seconnds) override;

            int getRealTime(int64_t* start_time, int64_t* current_time) override;
            int getMediaTime(int64_t* start_time, int64_t* current_time) override;

            bool OMXSetReferenceClock(bool has_audio, bool lock = true);
            int getPortDefinition(int port_index);

        private:
            int setScaleImpl(int scale);
            int onCallback(int event,
                    uint32_t data1,
                    uint32_t data2,
                    void* data);

            int getOmxClockState();

        private:
            std::shared_ptr<Component> clock_;
            int target_clock_scale {0};
            uint32_t enabled_port_mask_ {0};
            bool trick_mode_eanbled_ {false};

            OMX_U32 WaitMask;
            OMX_TIME_CLOCKSTATE   eState;
            OMX_TIME_REFCLOCKTYPE eClock {OMX_TIME_RefClockNone};
            int64_t last_media_time;
    };


} //namespace NDL_Esplayer {

#endif //#ifndef NDL_DIRECTMEDIA2_OMXCLOCK_H_
