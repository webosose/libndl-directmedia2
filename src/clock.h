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

#ifndef NDL_DIRECTMEDIA2_CLOCK_H_
#define NDL_DIRECTMEDIA2_CLOCK_H_

namespace NDL_Esplayer {

    class Clock {
        public:
            enum {NORMAL_PLAYBACK_RATE = 1000};
            enum {MAX_CLOCK_TOLERANCE = 50000};  // tuning value to update reference clock in ms
            virtual ~Clock() {};

            virtual int create() = 0;
            virtual void destroy() = 0;

            virtual int getPortDefinition(int port_index) {return 0;}
            virtual int reconfigureOutputBuffers() = 0;

            virtual int enablePort(int port_index, bool enable, int timeout_seconds) = 0;

            virtual int connectComponent(int port_index,
                    std::shared_ptr<Component> component,
                    int component_port_index) = 0;

            virtual int allocateOutputBuffer() = 0;
            virtual int freeOutputBuffer() = 0;

            virtual int stepFrame(uint32_t video_port_index) = 0;
            virtual int setPlaybackRate(int rate) = 0;//0:pause 0.5x:500 1x:1000 2x:2000
            virtual int setWaitingForStartTime(int port_index) = 0;
            virtual int setStopped() = 0;

            virtual int setState(OMX_STATETYPE state, int timeout_seconds) = 0;
            virtual int waitForState(OMX_STATETYPE state, int timeout_seconnds) = 0;

            virtual int getMediaTime(int64_t* start_time, int64_t* current_time) = 0;
            virtual int getRealTime(int64_t* start_time, int64_t* current_time) = 0;
            virtual int64_t getRenderDelay(int port_index, int64_t timestamp) { return 0; }
            virtual void updateMediaTime(int port_index, int64_t timestamp) {};
            virtual bool hasStartTime(int port_index) {return true;}
            virtual int getPlaybackRate() {return NORMAL_PLAYBACK_RATE;}
            virtual void setTrickMode(bool enable) {}

    };


} // namespace NDL_Esplayer {

#endif // #ifndef NDL_DIRECTMEDIA2_CLOCK_H_
