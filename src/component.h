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

#ifndef NDL_DIRECTMEDIA2_COMPONENT_H_
#define NDL_DIRECTMEDIA2_COMPONENT_H_

#define SUPPORT_ALSA_RENDERER_COMPONENT 0

#include <memory>
#include <functional>

#include "ndl-directmedia2/media-common.h"
#include "omx/omxclient.h"

namespace NDL_Esplayer {
    static const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};
    using ComponentCallback = std::function<void(int event,
            uint32_t data1,
            uint32_t data2,
            void* data)>;

    class Component : public OmxClient {



        public:
            explicit Component(ComponentCallback callback);
            virtual ~Component();


            enum COMPONENT {
                CLOCK,
                VIDEO_RENDERER,
                AUDIO_RENDERER,
                VIDEO_SCHEDEULER,
                AUDIO_MIXER,
            };

            enum EVENT {
                EVENT_RENDER = OMX_EventVendorStartUnused + 0x10000,
                EVENT_VIDEO_INFO,
                EVENT_AUDIO_INFO,
            };

            int create(COMPONENT component);
            int create(NDL_ESP_VIDEO_CODEC codec);
            int create(NDL_ESP_AUDIO_CODEC codec);

            void onOmxClientCallback(int event, uint32_t data1, uint32_t data2, void* data);

            int setResourceInfo(int audioPort, int videoPort, int mixerPort, int coreType);
            int setResourceInfo(int audioPort, int videoPort);
            int setAudioResourceInfo(int audioPort);
            int setVideoResourceInfo(int videoPort);
            int setVideoFormat(NDL_ESP_META_DATA *metadata);
            int setRenderFormat(int video_width, int video_height);
            int setAudioCodecFormat(int port_index, NDL_ESP_META_DATA *metadata);
            int setAudioFormat(int port_index, NDL_ESP_AUDIO_CODEC codec);
            int setDataFormat(OMX_AUDIO_PARAM_MP3TYPE data);
            int setDataFormat(OMX_AUDIO_PARAM_AACPROFILETYPE data);
            int setDataFormat(OMX_AUDIO_PARAM_PCMMODETYPE data);

            int connectOutputComponent(Component* output);

            int getInputPortIndex() const       { return port_index_input_ ; }
            int getClockInputPortIndex() const  { return port_index_clock_input_ ; }
            int getOutputPortIndex() const      { return port_index_output_ ; }

            int getInputBufferCount() const          { return input_buffer_count_; }
            int getInputBufferSize() const      { return input_buffer_size_; }
            int getOutputBufferCount() const          { return output_buffer_count_; }
            int getOutputBufferSize() const     { return output_buffer_size_; }

            int loadInitialPortSetting();
            int copyPCMFormat(int dst_port_index, Component* src, int src_port_index);

            //TODO: FIX Name. i.e. configureXXXXXBuffers.
            int configureInputBuffers(int count, int size) {
                int ret=0;
                if (port_index_input_>=0){
                    ret=reconfigureBuffers(port_index_input_,
                            count,
                            size);
                    input_buffer_count_=count;
                    input_buffer_size_=size;
                }
                return ret;
            }
            int configureOutputBuffers(int count, int size) {
                int ret=0;
                if (port_index_output_>=0){
                    ret=reconfigureBuffers(port_index_output_,
                            count,
                            size);
                    output_buffer_count_=count;
                    output_buffer_size_=size;
                }
                return ret;
            }
            int allocateInputBuffer() {
                if (port_index_input_>=0)
                    return allocateBuffer(port_index_input_,
                            input_buffer_count_,
                            input_buffer_size_);
                return 0;
            }
            int allocateOutputBuffer() {
                if (port_index_output_>=0)
                    return allocateBuffer(port_index_output_,
                            output_buffer_count_,
                            output_buffer_size_);
                return 0;
            }

            void printComponentInfo() const;

        private:
            void getVideoInfo(NDL_ESP_VIDEO_INFO_T* videoInfo, void* data);

        protected:
            ComponentCallback callback_;

            int port_index_input_ {-1};
            int port_index_clock_input_ {-1};
            int port_index_output_ {-1};

            int buffer_count_ {0};
            int input_buffer_count_ {0};
            int output_buffer_count_ {0};
            int input_buffer_size_ {0};
            int output_buffer_size_ {0};
    };

} //namespace NDL_Esplayer


#endif //#ifndef NDL_DIRECTMEDIA2_COMPONENT_H_
