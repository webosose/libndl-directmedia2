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

#ifndef NDL_DIRECTMEDIA2_OMX_CLIENTS_OMXCLIENT_H_
#define NDL_DIRECTMEDIA2_OMX_CLIENTS_OMXCLIENT_H_
#include <map>
#include <list>
#include <vector>
#include <stdint.h>
#include <string.h>
#include "omxcore.h"

#include <OMX_Core.h>
#include <OMX_Component.h>

#ifdef OMX_SKIP64BIT
static inline int64_t from_omx_time (OMX_TICKS t)
{
    int64_t p = 0;
    int num_shift = 32;

    p = t.nLowPart;
    p = p | ((uint64_t)(t.nHighPart) << num_shift);

    return p;
}
static inline OMX_TICKS to_omx_time (int64_t p)
{
    OMX_TICKS t;
    int num_shift = 32;

    t.nLowPart = p;
    t.nHighPart = p >> num_shift;

    return t;
}
#else
#define from_omx_time(x) (x)
#define to_omx_time(x) (x)
#endif

namespace NDL_Esplayer {

    /** Define the OMX IL version that corresponds to this set of header files.
     *  We also define a combined version that can be used to write or compare
     *  values of the 32bit nVersion field, assuming a little endian architecture */
#define OMX_VERSION_MAJOR 1
#define OMX_VERSION_MINOR 1
#define OMX_VERSION_REVISION 2
#define OMX_VERSION_STEP 0

#define OMX_VERSION ((OMX_VERSION_STEP<<24) | (OMX_VERSION_REVISION<<16) | (OMX_VERSION_MINOR<<8) | OMX_VERSION_MAJOR)


    /**
     * Time out value for synchronized set state command
     */
#define SYNC_STATE_TIMEOUT_SECS 3

    /**
     * Definition of callback event
     */
    typedef enum {
        OMX_CLIENT_EVT_EMPTY_BUFFER_DONE,
        OMX_CLIENT_EVT_FILL_BUFFER_DONE,
        OMX_CLIENT_EVT_PORT_SETTING_CHANGED,
        OMX_CLIENT_EVT_END_OF_STREAM,
        OMX_CLIENT_EVT_UNDERFLOW,
        OMX_CLIENT_EVT_RESOURCE_ACQUIRED,
    } OMX_CLIENT_EVENT;

#define omx_init_structure(ptr, type) \
    do { \
        memset((ptr), 0x0, sizeof(type)); \
        (ptr)->nSize = sizeof(type); \
        (ptr)->nVersion.s.nVersionMajor = 0x1; \
        (ptr)->nVersion.s.nVersionMinor = 0x1; \
        (ptr)->nVersion.s.nRevision = 0x2; \
        (ptr)->nVersion.s.nStep = 0x0; \
    } while(0);

    class OmxClient {
        public:
            typedef void (*player_listener_callback) (int event, uint32_t data1, uint32_t data2, void* data,  void* userdata );
            OmxClient(player_listener_callback listener, void* userdata);
            virtual ~OmxClient();

            /**
             * Buffer status : OWNED_BY_CLIENT -> free buffer, OWNED_BY_COMPONENT -> using by component
             */
            typedef enum {
                BUFFER_STATUS_OWNED_BY_CLIENT = 1,
                BUFFER_STATUS_OWNED_BY_COMPONENT,
            }BUFFER_STATUS;

            /**
             * Buffer info to be taken by pAppPrivate of OMX_BUFFERHEADERTYPE
             */
            struct BufferInfo {
                BUFFER_STATUS status;
                int port_index;
                int buffer_index;
            };

            /**
             * Get component handle by role
             */
            int createByRole(const char* role);
            int sendCommand(OMX_COMMANDTYPE command, int port_index);
            int createByName(const std::string & name);
            int writeToConfigBuffer(int port_index, const uint8_t* data, int32_t data_len);

            /**
             * Free component handle
             */
            void destroy();

            /**
             * Set state (it could be sync and async both) : LOADED,IDLE,PAUSE,EXECUTING
             */
            int setState(OMX_STATETYPE state, int timeout_seconds=0);
            /**
             * Get current state
             */
            OMX_STATETYPE getState();
            /**
             * Wait for state if the state is not syncronized
             */
            int waitForState(OMX_STATETYPE state, int timeout_seconds);
            /**
             * Wait for flushing if flush is called without syncronized
             */
            int waitForFlushing(int timeout_seconds);
            /**
             * Wait for port enable/disble done event if it is not syncronized
             */
            int waitForPortEnable(int port_index, bool enable, int timeout_seconds);
            /**
             * Wait for buffer state
             */
            int waitForBufferState(OMX_BUFFERHEADERTYPE* buffer, BUFFER_STATUS state, int timeout_seconds);
            /**
             * Port enable and disable
             */
            int enablePort(int port_index, bool enable, int timeout_seconds);
            /**
             *  Port flush
             */
            int flush(int port_index, int timeout_seconds=0);

            /**
             * Setting port parameters : it does not include buffer allocation
             */
            int reconfigureBuffers(int port_index, int buffer_count, int buffer_size);

            int getPortDefinition(int port_index);

            /**
             * get port parameters
             */
            int getParam(OMX_INDEXTYPE param_index, OMX_PTR param_data);

            /**
             * set port parameters
             */
            int setParam(OMX_INDEXTYPE param_index, OMX_PTR param_data);

            /**
             * Set Config : only for clock at this moment
             */
            int setConfig(OMX_INDEXTYPE config_index, OMX_PTR config_data);
            /**
             * Get Config : only for clock at this moment
             */
            int getConfig(OMX_INDEXTYPE config_index, OMX_PTR config_data);

            /**
             * SetupTunnel mode with out_port of me and in_port of destination component
             */
            int setupTunnel(int source_port_index, OmxClient* destination, int destination_port_index);

            /**
             * Free buffer
             */
            int freeBuffer(int port_index);
            /**
             * Allocate buffers in port_index
             */
            int allocateBuffer(int port_index, int buffer_count, int buffer_size);

            /**
             * Set UseBuffer at port index on me with buffer_owners's buffer_owner_port_index buffers
             */
            int useBuffer(int port_index, OmxClient* buffer_owner, int buffer_owner_port_index);

            /**
             * Feed data into free buffer
             */
            int writeToFreeBuffer(int port_index, const uint8_t* data, int32_t data_len, int64_t pts,uint32_t flags);
            /**
             * Feed data into free buffer for UseBuffer Mode
             */
            int writeToFreeBuffer(int port_index, int buffer_index, OmxClient* buffer_owner, int buffer_owner_port_index);


            /**
             * Get buffer status at pAppPrivate of buf
             */
            inline BUFFER_STATUS getBufferStatus(const OMX_BUFFERHEADERTYPE* buf) const {
                const BufferInfo* info = (const BufferInfo*)buf->pAppPrivate;
                return info->status;
            }
            /**
             * Set buffer status at pAppPrivate of buf
             */
            inline void setBufferStatus(OMX_BUFFERHEADERTYPE* buf, BUFFER_STATUS status) {
                BufferInfo* info = (BufferInfo*)buf->pAppPrivate;
                info->status = status;
            }

            /**
             * Get buffer status at buffer_index of port_index
             */
            inline bool isFreeBufferIndex(const int port_index, const int buffer_index) {
                auto buffers = port_buffers_.at(port_index);
                return (getBufferStatus(buffers.at(buffer_index)) == BUFFER_STATUS_OWNED_BY_CLIENT);
            }

            /**
             * Get first free buffer (buffer status : BUFFER_STATUS_OWNED_BY_CLIENT)
             */
            int getFreeBufferIndex(int port_index) const;

            /**
             * Get the number of free buffers (buffer status : BUFFER_STATUS_OWNED_BY_CLIENT)
             */
            int getFreeBufferCount(int port_index) const;

            /**
             * Get the number of used buffers (buffer status : BUFFER_STATUS_OWNED_BY_COMPONENT)
             */
            int getUsedBufferCount(int port_index) const;

            /**
             * Return buffer at buffer_index of port_index
             */
            OMX_BUFFERHEADERTYPE* getBuffer(int port_index, int buffer_index) ;

            /**
             * Do EmptyThisBuffer at buffer_index of port_index
             */
            int emptyBuffer(int port_index, int buffer_index) ;
            /**
             * Do FillThisBuffer at buffer_index of port_index
             */
            int fillBuffer(int port_index, int buffer_index);

            /**
             * Get the Component name for debugging
             */
            const char* getComponentName() const {
                return component_name_;
            }

            /**
             * Destroy OmxCore signle-tone instance
             */
            static void destroyCoreInstance() {
                OmxCore::destroyInstance();
            }

            /**
             * Map operation
             */
            bool insertPortMap(int portIdx);
            bool getPortMapState(int portIdx);
            bool setPortMapState(int portIdx, bool state);
            void printPortMap() const;

        private:
            /**
             * callback from OMX components
             */
            static OMX_ERRORTYPE EventHandler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                    OMX_EVENTTYPE event, OMX_U32 data1, OMX_U32 data2, OMX_PTR eventdata) {
                return ((OmxClient*)pAppData)->eventHandler(event, data1, data2, eventdata);
            }

            static OMX_ERRORTYPE EmptyBufferDone(OMX_HANDLETYPE hComponent,
                    OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer) {
                return ((OmxClient*)pAppData)->emptyBufferDone(pBuffer);
            }
            static OMX_ERRORTYPE FillBufferDone(OMX_HANDLETYPE hComponent,
                    OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer) {
                return ((OmxClient*)pAppData)->fillBufferDone(pBuffer);
            }

            OMX_ERRORTYPE eventHandler(OMX_EVENTTYPE event, OMX_U32 data1, OMX_U32 data2, OMX_PTR eventdata) ;
            OMX_ERRORTYPE emptyBufferDone(OMX_BUFFERHEADERTYPE* buf) ;
            OMX_ERRORTYPE fillBufferDone(OMX_BUFFERHEADERTYPE* buf);

        private:
            player_listener_callback player_listener_;
            void* userdata_;
            OMX_HANDLETYPE component_handle_;

            uint32_t enabled_port_mask_;
            std::map<int, bool> enabled_port_map_;
            std::list<int> flushing_;
            uint32_t enabling_port_mask_;
            uint32_t enable_set_buffer_port_;
            char* component_name_;
            OMX_STATETYPE current_state_;

            pthread_mutex_t state_lock_;
            pthread_cond_t state_cond_;
            pthread_condattr_t state_attr_;

            pthread_mutex_t buffer_lock_;
            pthread_cond_t buffer_cond_;
            pthread_condattr_t buffer_attr_;

            std::map<int, std::vector<OMX_BUFFERHEADERTYPE*>> port_buffers_;

            std::vector<OMX_BUFFERHEADERTYPE*>& getBuffers(int port_index) {
                return port_buffers_.at(port_index);
            }

            OmxClient(OmxClient const&) = delete;
            void operator=(OmxClient const&) = delete;

    };

} //namespace NDL_Esplayer

#endif //NDL_DIRECTMEDIA2_OMX_CLIENTS_OMXCLIENT_H_
