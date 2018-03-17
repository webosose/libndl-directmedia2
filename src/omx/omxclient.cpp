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
#include <algorithm>

#include "omxclient.h"

using namespace NDL_Esplayer;

#define LOGTAG component_name_?component_name_:"OmxClient"
#include "debug.h"

#define LOG_CALLBACK  NDL_LOGV
#define LOG_STATUS  NDL_LOGV
#define LOG_BUFFER  NDL_LOGV
#define LOG_BUFFER_STATUS NDL_LOGV

OmxClient::OmxClient(player_listener_callback listener, void* userdata)
    : player_listener_(listener)
    , userdata_(userdata)
    , component_handle_(0)
    , enabled_port_mask_(0)
    , enabling_port_mask_(0)
    , enable_set_buffer_port_(0)
    , component_name_(0)
    , current_state_(OMX_StateInvalid)
{
    // initialize mutex for state
    pthread_mutex_init(&state_lock_, NULL);
    pthread_condattr_init(&state_attr_);
    pthread_condattr_setclock(&state_attr_ , CLOCK_MONOTONIC);
    pthread_cond_init(&state_cond_, &state_attr_);

    // initialize mutex for buffer resource
    pthread_mutex_init(&buffer_lock_, NULL);
    pthread_condattr_init(&buffer_attr_);
    pthread_condattr_setclock(&buffer_attr_ , CLOCK_MONOTONIC);
    pthread_cond_init(&buffer_cond_, &buffer_attr_);
}

OmxClient::~OmxClient()
{
    // destroy mutex for state
    pthread_mutex_destroy(&state_lock_);
    pthread_condattr_destroy(&state_attr_);
    pthread_cond_destroy(&state_cond_);

    // destroy mutex for buffer resource
    pthread_mutex_destroy(&buffer_lock_);
    pthread_condattr_destroy(&buffer_attr_);
    pthread_cond_destroy(&buffer_cond_);

    // clear port map
    enabled_port_map_.clear();

    // clear flushing list
    flushing_.clear();
}

int OmxClient::createByName(const std::string & name)
{
    OMX_ERRORTYPE err = OMX_ErrorNone;
    static OMX_CALLBACKTYPE callbacks = {
        OmxClient::EventHandler,
        OmxClient::EmptyBufferDone,
        OmxClient::FillBufferDone
    };
    if (name == "OMX.drm.video_render") {
        err = OmxCore::getInstance().getDRMHandle(&component_handle_,
                (OMX_STRING)name.c_str(),
                (OMX_PTR)this, &callbacks);
    } else if(name == "OMX.alsa.audio_render"){
        err = OmxCore::getInstance().getALSAHandle(&component_handle_,
                (OMX_STRING)name.c_str(),
                (OMX_PTR)this, &callbacks);
    } else {
        err = OmxCore::getInstance().getHandle(&component_handle_,
                (OMX_STRING)name.c_str(),
                (OMX_PTR)this, &callbacks);
    }

    if (err == OMX_ErrorNone)
    {
        component_name_ = new char[name.size() + 1];
        std::copy(name.begin(), name.end(), component_name_);
        component_name_[name.size()] = '\0';
        NDLLOG(LOGTAG, NDL_LOGI, "createByName - component name : %s", component_name_);
    }

    return 0;
}

int OmxClient::sendCommand(OMX_COMMANDTYPE command, int port_index)
{
    NDLASSERT(component_handle_);
    OMX_ERRORTYPE err = OMX_ErrorNone;
    err = OMX_SendCommand(component_handle_, command, port_index, NULL);
    if (err != OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGE, "OMX sendCommand  error : 0x%x",err);
        return err;
    }
    return 0;
}

int OmxClient::writeToConfigBuffer(int port_index,
        const uint8_t* data,
        int32_t data_len)
{
    NDLLOG(LOGTAG, LOG_BUFFER_STATUS, "writeToConfigBuffer (port : %d, len : %d)",
            port_index, data_len);
    pthread_mutex_lock(&buffer_lock_);
    int buffer_index = getFreeBufferIndex(port_index);
    if(buffer_index < 0)
    {
        pthread_mutex_unlock(&buffer_lock_);
        return -1;
    }
    OMX_BUFFERHEADERTYPE* buf = getBuffer(port_index, buffer_index);

    if(!buf) {
        pthread_mutex_unlock(&buffer_lock_);
        return -1;
    }

    buf->nOffset = 0;
    buf->nFilledLen = data_len;
    memset((unsigned char *)buf->pBuffer, 0x0, buf->nAllocLen);
    memcpy((unsigned char *)buf->pBuffer, data, buf->nFilledLen);
    buf->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;

    if (emptyBuffer(port_index, buffer_index)==OMX_ErrorNone)
    {
        pthread_mutex_unlock(&buffer_lock_);
        //TODO Need to change below line after checking SIC Audio Decoder don't modify nFilledLen
        //         SIC Audio Decoder modify nFilledLen sometimes. 2015.04.02
        //         data_len -> buf->nFilledLen
        return data_len;
    }

    pthread_mutex_unlock(&buffer_lock_);
    return 0;
}

int OmxClient::createByRole(const char* role)
{
    OMX_U32 num_comps = 0;
    OMX_U8** names = NULL;
    OMX_U8*  component_names = NULL;
    OMX_ERRORTYPE err = OMX_ErrorNone;
    uint32_t i = 0;
    static OMX_CALLBACKTYPE callbacks = {
        OmxClient::EventHandler,
        OmxClient::EmptyBufferDone,
        OmxClient::FillBufferDone
    };

    NDLLOG(LOGTAG, LOG_STATUS, "createByRole (%s) ", role);
    err = OmxCore::getInstance().getComponentsOfRole((OMX_STRING)role, &num_comps, NULL);
    if (err != OMX_ErrorNone)
        return err;

    if (num_comps == 0)
        return -1;

    names = (OMX_U8**)calloc(sizeof(OMX_U8*), num_comps);
    if (names==NULL)
        return OMX_ErrorInsufficientResources;

    component_names = (OMX_U8*)calloc(OMX_MAX_STRINGNAME_SIZE, num_comps);
    if (component_names==NULL)
    {
        free(names);
        return OMX_ErrorInsufficientResources;
    }

    for (i=0; i<num_comps; ++i)
    {
        names[i] = component_names+(i*OMX_MAX_STRINGNAME_SIZE);
    }
    err = OmxCore::getInstance().getComponentsOfRole((OMX_STRING)role,
            &num_comps, names);
    if (err != OMX_ErrorNone)
    {
        free(names);
        free(component_names);
        return err;
    }

    for (i=0; i<num_comps; ++i)
    {
        err = OmxCore::getInstance().getHandle(&component_handle_,
                (OMX_STRING)names[i],
                (OMX_PTR)this, &callbacks);
        if (err==OMX_ErrorNone)
        {
            component_name_ = (char*)calloc(1,strlen((char*)names[i])+1);
            memcpy((void*)component_name_, names[i], strlen((char*)names[i]));
            NDLLOG(LOGTAG, LOG_STATUS, "createByRole : component name : %s",
                    component_name_);
            break;
        }
    }

    free(names);
    free(component_names);
    return 0;
}

void OmxClient::destroy()
{
    NDLLOG(LOGTAG, LOG_STATUS, "destroy ");
    NDLLOG(LOGTAG, NDL_LOGI, "Destroy component name : %s", component_name_);
    if (component_handle_)
    {
        OMX_ERRORTYPE err = OMX_ErrorNone;
        if (!strcmp(component_name_,"OMX.drm.video_render"))
            err = OmxCore::getInstance().freeDRMHandle(component_handle_);
        else
            err = OmxCore::getInstance().freeHandle(component_handle_);

        if (err == OMX_ErrorNone)
            NDLLOG(LOGTAG, LOG_STATUS, "Destroy done!!!! ");
        component_handle_ = NULL;
    }

    if (component_name_)
    {
        delete component_name_;
        component_name_ = NULL;
    }
}

//sendCommand
int OmxClient::setState(OMX_STATETYPE state, int timeout_seconds)
{
    NDLASSERT(component_handle_);
    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_STATETYPE previous_state = current_state_;

    if (getState() == state) {
        NDLLOG(LOGTAG, NDL_LOGI, "skip the setState because of same state.");
        return err;
    }

    NDLLOG(LOGTAG, LOG_STATUS, "setState (0x%x) ", state);
    current_state_ = state;
    err = OMX_SendCommand(component_handle_, OMX_CommandStateSet, state, 0);

    if (err == OMX_ErrorNone) {
        if ( timeout_seconds ) {
            return waitForState(state, timeout_seconds);//SYNC_STATE_TIMEOUT_SECS);
        }
        return 0;
    }
    current_state_ = previous_state;
    return err;
}

OMX_STATETYPE OmxClient::getState()
{
    NDLASSERT(component_handle_);
    OMX_STATETYPE state;
    OMX_GetState(component_handle_, &state);
    return state;
}

int OmxClient::waitForState(OMX_STATETYPE state, int timeout_seconds)
{
    timespec ts;
    int err = 0;
    NDLLOG(LOGTAG, LOG_STATUS, "%s in (current state : %d, request state : %d)",
            __func__, getState(), state);
    if (getState() == state) {
        NDLLOG(LOGTAG, NDL_LOGI, "%s Done setState (0x%x)", __func__, state);
        return err;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "system : clock_gettime error...... ");
        return -1;
    }
    ts.tv_sec += timeout_seconds;

    do {
        pthread_mutex_lock(&state_lock_);
        err = pthread_cond_timedwait(&state_cond_, &state_lock_, &ts);
        pthread_mutex_unlock(&state_lock_);
        NDLLOG(LOGTAG, LOG_STATUS, "%s unlock, state : %d, err : %d",
                __func__, getState(), err);
    } while(err == 0 && getState() != state);

    if (err == ETIMEDOUT)
        NDLLOG(LOGTAG, NDL_LOGE, "timed out while waiting %d state  ...... ", state);
    return err;
}

int OmxClient::waitForFlushing(int timeout_seconds)
{
    timespec ts;
    int err = 0;
    NDLLOG(LOGTAG, LOG_STATUS, "%s in (flushing port count: %d)", __func__, flushing_.size());
    if (flushing_.empty())
        return err;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "system : clock_gettime error...... ");
        return -1;
    }
    ts.tv_sec += timeout_seconds;

    do {
        pthread_mutex_lock(&state_lock_);
        err = pthread_cond_timedwait(&state_cond_, &state_lock_, &ts );
        pthread_mutex_unlock(&state_lock_);
        NDLLOG(LOGTAG, LOG_STATUS, "%s unlock, flushing port count: %d, err:%d, is_empty:%d", __func__, flushing_.size(), err, flushing_.empty());
    } while(err == 0 && flushing_.empty() != true);

    if (err == ETIMEDOUT)
        NDLLOG(LOGTAG, NDL_LOGE, "timed out while flushing ...... ");
    return err;
}

int OmxClient::waitForPortEnable(int port_index, bool enable,  int timeout_seconds)
{
    timespec ts;
    int err = 0;
    NDLLOG(LOGTAG, LOG_STATUS, "%s port:%d in (port_map check > enabled ? %d, request : %d)",
            __func__, port_index, getPortMapState(port_index), enable);
    if (enable == getPortMapState(port_index))
        return err;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "system : clock_gettime error...... ");
        return -1;
    }
    ts.tv_sec += timeout_seconds;

    do {
        pthread_mutex_lock(&state_lock_);
        err = pthread_cond_timedwait(&state_cond_, &state_lock_, &ts);
        pthread_mutex_unlock(&state_lock_);
        NDLLOG(LOGTAG, LOG_STATUS, "%s unlock, enabled_port_map_ : %d, err : %d ",
                __func__, getPortMapState(port_index), err);
    } while(err == 0 && (enable != getPortMapState(port_index)));

    if (err == ETIMEDOUT)
        NDLLOG(LOGTAG, NDL_LOGE, "timed out while waiting %d port enable >> %d  ...... ", port_index, enable);
    return err;
}

int OmxClient::waitForBufferState(OMX_BUFFERHEADERTYPE* buffer,  BUFFER_STATUS state, int timeout_seconds)
{
    timespec ts;
    int err = 0;

    if (getBufferStatus(buffer) == state)
        return err;

    NDLLOG(LOGTAG, LOG_STATUS, "%s buf:%p (current state : %d, request state : %d)",
            __func__, buffer, getBufferStatus(buffer), state);

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        NDLLOG(LOGTAG, NDL_LOGE, "system : clock_gettime error...... ");
        return -1;
    }
    ts.tv_sec += timeout_seconds;

    do {
        pthread_mutex_lock(&buffer_lock_);
        err = pthread_cond_timedwait(&buffer_cond_, &buffer_lock_, &ts);
        pthread_mutex_unlock(&buffer_lock_);
        NDLLOG(LOGTAG, LOG_STATUS, "%s unlock, state : %d, err : %d",
                __func__, getBufferStatus(buffer), err);
    } while(err == 0 && getBufferStatus(buffer) != state);

    if (err == ETIMEDOUT)
        NDLLOG(LOGTAG, NDL_LOGE, "timed out while waiting %d buffer state  ...... ", state);
    return err;
}

int OmxClient::enablePort(int port_index, bool enable, int timeout_seconds)
{
    //TODO check whether it is needed to be synchronized
    NDLASSERT(component_handle_);
    NDLLOG(LOGTAG, LOG_STATUS, "enablePort  (port : %d, %s) ",
            port_index, enable?"enable":"disable");
#if 0 //FIXME : check it is needed or not. It suppose to be ok without this.
    if (enable) {
        if (enabled_port_mask_ & (1<<port_index)!= 0) {
            NDLLOG(LOGTAG, NDL_LOGI, "already enabled port %d (portmask : 0x%x)", port_index, enabled_port_mask_);
            return OMX_ErrorNone;
        }
    }
    else if ((enabled_port_mask_ & (1<<port_index)) == 0) {
        NDLLOG(LOGTAG, NDL_LOGI, "already disabled port %d (portmask : 0x%x)", port_index, enabled_port_mask_);
        return OMX_ErrorNone;
    }
#endif

    OMX_COMMANDTYPE cmd;
    if (enable) cmd = OMX_CommandPortEnable;
    else        cmd = OMX_CommandPortDisable;

    OMX_ERRORTYPE ret = OMX_SendCommand(component_handle_, cmd, port_index, 0);
    if (ret == OMX_ErrorNone) {
        if ( timeout_seconds )
            return  waitForPortEnable(port_index, enable, timeout_seconds);
    }
    return ret;
}

int OmxClient::flush(int port_index, int timeout_seconds)
{
    NDLASSERT(component_handle_);
    NDLLOG(LOGTAG, LOG_STATUS, "flush (flushing port count: %d) ", flushing_.size());
    OMX_ERRORTYPE err = OMX_ErrorNone;
    if (!flushing_.empty())
    {
        NDLLOG(LOGTAG, LOG_STATUS, "do nothing..  already flushing. try after finish flusing");
        return -1;
    }

    if ((uint32_t)port_index == OMX_ALL) {
        // push the enabled port index to flushing_ list for OMX ALL
        for (auto& iter : enabled_port_map_) {
            if (iter.second) {
                NDLLOG(LOGTAG, NDL_LOGD, "Push the PortNum(%d) for flushing", iter.first);
                flushing_.push_back(iter.first);
            }
        }
    } else {
        flushing_.push_back(port_index);
    }

    err = OMX_SendCommand(component_handle_, OMX_CommandFlush, port_index, 0);
    if (err!=OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGD, "Flush command error. clear flushing list");
        flushing_.clear();
        return err;
    }

    if (timeout_seconds)
        return waitForFlushing(timeout_seconds);

    return 0;
}

int OmxClient::getPortDefinition(int port_index)
{
    NDLASSERT(component_handle_);
    OMX_ERRORTYPE ret;
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    omx_init_structure(&port_def, OMX_PARAM_PORTDEFINITIONTYPE);
    port_def.nPortIndex = port_index;
    ret = OMX_GetParameter(component_handle_, OMX_IndexParamPortDefinition, &port_def);
    if (ret != OMX_ErrorNone)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "Unable to get port %d th definition.... ", port_index);
        return -1;
    }
    else {
        NDLLOG(LOGTAG, LOG_STATUS,
                "port:%d info. (cnt:%d, size:%d)",
                port_index, port_def.nBufferCountActual, port_def.nBufferSize);
        return ret;
    }
}

int OmxClient::reconfigureBuffers(int port_index, int buffer_count, int buffer_size)
{
    NDLASSERT(component_handle_);
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    omx_init_structure(&port_def, OMX_PARAM_PORTDEFINITIONTYPE);
    port_def.nPortIndex = port_index;
    if (OMX_GetParameter(component_handle_, OMX_IndexParamPortDefinition,
                &port_def)  != OMX_ErrorNone)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "Unable to get port %d th definition.... ", port_index);
        return -1;
    }

    NDLLOG(LOGTAG, LOG_STATUS,
            "reconfigureBuffers port:%d from (cnt:%d, size:%d) to (buffer cnt : %d, buffer size : %d)",
            port_index, port_def.nBufferCountActual, port_def.nBufferSize,
            buffer_count, buffer_size);

    port_def.nBufferCountActual = buffer_count;
    port_def.nBufferSize = buffer_size;

    return OMX_SetParameter(component_handle_,
            OMX_IndexParamPortDefinition,
            &port_def );
}

int OmxClient::setParam(OMX_INDEXTYPE param_index, OMX_PTR param_data)
{
    NDLASSERT(component_handle_);
    NDLLOG(LOGTAG, LOG_STATUS, "setParam (param index : 0x%x) ", param_index);
    return OMX_SetParameter(component_handle_, param_index, param_data);
}

int OmxClient::getParam(OMX_INDEXTYPE param_index, OMX_PTR param_data)
{
    NDLASSERT(component_handle_);
    NDLLOG(LOGTAG, LOG_STATUS, "getParam (param index : 0x%x) ", param_index);
    return OMX_GetParameter(component_handle_, param_index, param_data);
}


int OmxClient::setConfig(OMX_INDEXTYPE config_index, OMX_PTR config_data)
{
    NDLASSERT(component_handle_);
    NDLLOG(LOGTAG, LOG_STATUS, "setConfig (config index : 0x%x) ", config_index);
    return OMX_SetConfig(component_handle_, config_index, config_data);
}

int OmxClient::getConfig(OMX_INDEXTYPE config_index, OMX_PTR config_data)
{
    NDLASSERT(component_handle_);
    NDLLOG(LOGTAG, LOG_STATUS, "getConfig (config index : 0x%x) ", config_index);
    return OMX_GetConfig(component_handle_, config_index, config_data);
}

int OmxClient::setupTunnel(int source_port_index,
        OmxClient* destination,
        int destination_port_index)
{
    NDLASSERT(component_handle_);
    NDLLOG(LOGTAG, LOG_STATUS, "setupTunnel (my port : %d, dest port : %d) ",
            source_port_index, destination_port_index);
    OMX_ERRORTYPE ret = OmxCore::getInstance().setupTunnel(this->component_handle_,
            source_port_index,
            destination->component_handle_,
            destination_port_index);
    if(ret == OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGD, "setupTunnel ok");
        //abled_port_mask_ |= 1<<source_port_index;
        //stination->enabled_port_mask_ |= 1<<destination_port_index;
    }

    ret = (OMX_ERRORTYPE)this->sendCommand(OMX_CommandPortEnable, source_port_index);
    if (ret != OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGE, "fail to enable src port(%d) : 0x%x",ret, source_port_index);
        return ret;
    }

    ret = (OMX_ERRORTYPE)destination->sendCommand(OMX_CommandPortEnable, destination_port_index);
    if (ret != OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGE, "fail to enable dest port(%d) : 0x%x",ret, destination_port_index);
        return ret;
    }
    return ret;
}

int OmxClient::freeBuffer(int port_index)
{
    NDLLOG(LOGTAG, LOG_BUFFER, "freeBuffer (port : %d) ", port_index);
    if (enablePort(port_index, false, 0)!=OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGE, "%dth port disble failed.", port_index);
        return -1;
    }

    auto buffers = port_buffers_.at(port_index);
    for(auto i=buffers.begin(); i!=buffers.end(); ++i) {
        BufferInfo* info = (BufferInfo*)(*i)->pAppPrivate;
        waitForBufferState(*i,  BUFFER_STATUS_OWNED_BY_CLIENT, 1);
        (*i)->pAppPrivate = NULL;
        delete info;
        OMX_FreeBuffer(component_handle_, port_index, *i);
    }
    port_buffers_.erase(port_index);
    return 0;
}

int OmxClient::allocateBuffer(int port_index, int buffer_count, int buffer_size)
{
    NDLLOG(LOGTAG, LOG_BUFFER, "allocateBuffer (port : %d, count : %d, size : %d) ",
            port_index, buffer_count, buffer_size);
    if (enablePort(port_index, true, 0)!=OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGE, "%dth port enable failed.", port_index);
        return -1;
    }

    std::vector<OMX_BUFFERHEADERTYPE*> buffers;
    for(int i=0; i<buffer_count; ++i) {
        OMX_BUFFERHEADERTYPE* buf;
        OMX_ERRORTYPE err = OMX_AllocateBuffer(component_handle_, &buf, port_index, NULL, buffer_size);
        if (err != OMX_ErrorNone) {
            NDLLOG(LOGTAG, NDL_LOGE, "CRASH due to OMX_AllocateBuffer(..., port:%d, bufSize=:%d) failure, err:0x%x!!!", port_index, buffer_size, err);
        }
        BufferInfo* info = new BufferInfo;
        info->status = BUFFER_STATUS_OWNED_BY_CLIENT;
        info->port_index = port_index ;
        info->buffer_index = i;
        buf->pAppPrivate = (OMX_PTR)info;
        buffers.push_back(buf);
    }
    port_buffers_[port_index] = buffers;
    NDLLOG(LOGTAG, LOG_BUFFER, "allocateBuffer done");
    return 0;
}

int OmxClient::useBuffer(int port_index,
        OmxClient* buffer_owner,
        int buffer_owner_port_index)
{
    NDLLOG(LOGTAG, LOG_BUFFER, "useBuffer (port : %d, owner port : %d) ",
            port_index, buffer_owner_port_index);
    auto item = buffer_owner->port_buffers_.find(buffer_owner_port_index);
    if(item == buffer_owner->port_buffers_.end()) {
        NDLLOG(LOGTAG, NDL_LOGE,
                "useBuffer (port : %d, owner port : %d), buffer owner has no buffer on port:%d ",
                port_index, buffer_owner_port_index, buffer_owner_port_index);
        return -1;
    }
    auto owner_buffers = (*item).second;
    if (enablePort(port_index, true, 0)!=OMX_ErrorNone) {
        NDLLOG(LOGTAG, NDL_LOGE, "%dth port enable failed.", port_index);
        return -1;
    }

    int index = 0;
    int err = OMX_ErrorNone;
    std::vector<OMX_BUFFERHEADERTYPE*> buffers;
    for(auto i=owner_buffers.begin(); i!=owner_buffers.end(); ++i, ++index) {
        OMX_BUFFERHEADERTYPE* buf;
        OMX_BUFFERHEADERTYPE* owner_buf = *i;

        err = OMX_UseBuffer(component_handle_, &buf, port_index, NULL,
                owner_buf->nAllocLen, owner_buf->pBuffer);
        if (err != OMX_ErrorNone)
            return err;
        BufferInfo* info = new BufferInfo;
        info->status = BUFFER_STATUS_OWNED_BY_CLIENT;
        info->port_index = port_index ;
        info->buffer_index = index;
        buf->pAppPrivate = (OMX_PTR)info;
        buffers.push_back(buf);
    }
    port_buffers_[port_index] = buffers;
    return 0;
}

int OmxClient::getFreeBufferIndex(int port_index) const
{
    int index = 0;
    auto buffers = port_buffers_.at(port_index);
    for(auto i=buffers.begin(); i!=buffers.end(); ++i, ++index) {
        if(getBufferStatus(buffers.at(index)) == BUFFER_STATUS_OWNED_BY_CLIENT) {
            NDLLOG(LOGTAG, LOG_BUFFER_STATUS, "getFreeBufferIndex (port : %d) return %d", port_index, index);
            return index;
        }
    }
    return -1;
}

int OmxClient::getFreeBufferCount(int port_index) const
{
    auto item = port_buffers_.find(port_index);
    if(item == port_buffers_.end()) {
        NDLLOG(LOGTAG, NDL_LOGE, "getFreeBufferCount (port : %d), no port", port_index);
        return 0;
    }
    int count = 0;
    auto buffers = port_buffers_.at(port_index);
    for(auto i=buffers.begin(); i!=buffers.end(); ++i) {
        if(getBufferStatus(*i) == BUFFER_STATUS_OWNED_BY_CLIENT)
            count += 1;
    }
    NDLLOG(LOGTAG, LOG_BUFFER_STATUS, "getFreeBufferCount (port : %d) return %d", port_index, count);
    return count;
}

int OmxClient::getUsedBufferCount(int port_index) const
{
    int count = 0;
    auto buffers = port_buffers_.at(port_index);
    for(auto i=buffers.begin(); i!=buffers.end(); ++i) {
        if(getBufferStatus(*i) == BUFFER_STATUS_OWNED_BY_COMPONENT)
            count += 1;
    }
    NDLLOG(LOGTAG, LOG_BUFFER_STATUS, "getUsedBufferCount(port : %d) return %d", port_index, count);
    return count;
}

OMX_BUFFERHEADERTYPE* OmxClient::getBuffer(int port_index, int buffer_index)
{
    auto item = port_buffers_.find(port_index);
    if(item == port_buffers_.end()) {
        NDLLOG(LOGTAG, NDL_LOGE,
                "getBuffer (port : %d, buffer index: %d), no buffer on port:%d ",
                port_index, buffer_index, port_index);
        return nullptr;
    }

    auto buffers = (*item).second;
    if(buffers.size() <= (size_t)buffer_index) {
        NDLLOG(LOGTAG, NDL_LOGE,
                "getBuffer (port : %d, buffer index: %d), index:%d >= size:%d ",
                port_index, buffer_index, buffer_index, buffers.size());
        return nullptr;
    }

    OMX_BUFFERHEADERTYPE* buf = buffers.at(buffer_index);
    return buf;
}

int OmxClient::emptyBuffer(int port_index, int buffer_index)
{
    //struct timeval startTime, endTime;
    //int32_t elapse = 0;
    OMX_BUFFERHEADERTYPE* buf = getBuffer(port_index, buffer_index);
    if (!buf)
        return -1;

    NDLLOG(LOGTAG, LOG_BUFFER, "%s port:%d, buf_idx:%d, pts:%d, filled_len:%d, flags:0x%x", __func__, port_index, buffer_index, buf->nTimeStamp.nLowPart, buf->nFilledLen, buf->nFlags);
    // buffer has to be owned by CLIENT so it is error state if buffer is owned by COMPONENT
    if (!isFreeBufferIndex(port_index, buffer_index)) {
        NDLLOG(LOGTAG, NDL_LOGE, "Buffer is already used by Component : emptyBuffer(port : %d, buffer index : %d)",
                port_index, buffer_index);
        return -1;
    }

    setBufferStatus(buf, BUFFER_STATUS_OWNED_BY_COMPONENT);
    //GETTIME(&startTime, NULL);
    int ret = OMX_EmptyThisBuffer(component_handle_, buf);
    //GETTIME(&endTime, NULL);
    //TIME_DIFF(startTime, endTime, elapse);
    //NDLLOG(LOGTAG, LOG_BUFFER, "OMX_EmptyThisBuffer elapsed:%dns", elapse);
    if (ret != OMX_ErrorNone) {
        setBufferStatus(buf, BUFFER_STATUS_OWNED_BY_CLIENT);
        NDLLOG(LOGTAG, NDL_LOGE, "OMX_EmptyThisBuffer return error : 0x%x", ret);
    }

    return ret;
}

int OmxClient::fillBuffer(int port_index, int buffer_index)
{
    NDLLOG(LOGTAG, LOG_BUFFER, "fillBuffer (port : %d, buffer index : %d)",
            port_index, buffer_index);
    OMX_BUFFERHEADERTYPE* buf = getBuffer(port_index, buffer_index);
    if (!buf)
        return -1;

    // buffer has to be owned by CLIENT so it is error state if buffer is owned by COMPONENT
    if (!isFreeBufferIndex(port_index, buffer_index)) {
        NDLLOG(LOGTAG, NDL_LOGE, "Buffer is already used by Component : fillBuffer (port : %d, buffer index : %d)",
                port_index, buffer_index);
        return -1;
    }

    buf->nFlags = 0;
    buf->nFilledLen = 0;
    buf->nTimeStamp = to_omx_time(0);

    setBufferStatus(buf, BUFFER_STATUS_OWNED_BY_COMPONENT);
    int ret = OMX_FillThisBuffer(component_handle_, buf);
    if (ret != OMX_ErrorNone) {
        setBufferStatus(buf, BUFFER_STATUS_OWNED_BY_CLIENT);
        NDLLOG(LOGTAG, NDL_LOGE, "OMX_FillThisBuffer return error : 0x%x", ret);
    }

    return ret;
}

int OmxClient::writeToFreeBuffer(int port_index,
        const uint8_t* data,
        int32_t data_len,
        int64_t pts,
        uint32_t flags)
{

    NDLLOG(LOGTAG, LOG_BUFFER_STATUS, "writeToFreeBuffer (port : %d, len : %d, pts : %lld)",
            port_index, data_len, pts);
    pthread_mutex_lock(&buffer_lock_);
    int buffer_index = getFreeBufferIndex(port_index);
    if(buffer_index < 0)
    {
        pthread_mutex_unlock(&buffer_lock_);
        return -1;
    }
    OMX_BUFFERHEADERTYPE* buf = getBuffer(port_index, buffer_index);
    if(!buf) {
        pthread_mutex_unlock(&buffer_lock_);
        return -1;
    }

    if (buf->nAllocLen >= (OMX_U32)data_len)
    {
        buf->nFilledLen = data_len;
        buf->nFlags = flags;
        memcpy(buf->pBuffer, data, data_len);
    }
    else
    {
        buf->nFilledLen = buf->nAllocLen;
        //TODO Need to remove below line after checking SIC Audio Decoder don't modify nFilledLen
        //         SIC Audio Decoder modify nFilledLen sometimes. 2015.04.02
        data_len = buf->nFilledLen;
        buf->nFlags = flags & ~OMX_BUFFERFLAG_ENDOFFRAME;
        memcpy(buf->pBuffer, data, buf->nAllocLen);
    }
    buf->nOffset = 0;

    //NDLLOG(LOGTAG, LOG_BUFFER, "[OMXClient]before into ToOMXTime (pts : %lld) ", pts);
    buf->nTimeStamp = to_omx_time((uint64_t)pts);
    //NDLLOG(LOGTAG, LOG_BUFFER, "[OMXClient]before into OMX (high:%d, low:%d) ", buf->nTimeStamp.nHighPart, buf->nTimeStamp.nLowPart);

    if (emptyBuffer(port_index, buffer_index)==OMX_ErrorNone)
    {
        pthread_mutex_unlock(&buffer_lock_);
        //TODO Need to change below line after checking SIC Audio Decoder don't modify nFilledLen
        //         SIC Audio Decoder modify nFilledLen sometimes. 2015.04.02
        //         data_len -> buf->nFilledLen
        return data_len;
    }
    pthread_mutex_unlock(&buffer_lock_);
    return 0;
}

int OmxClient::writeToFreeBuffer(int port_index,
        int buffer_index,
        OmxClient* buffer_owner,
        int buffer_owner_port_index)
{
    pthread_mutex_lock(&buffer_lock_);
    OMX_BUFFERHEADERTYPE* buf = getBuffer(port_index, buffer_index);
    if(!buf)
    {
        NDLLOG(LOGTAG, NDL_LOGE, "%s:buffer null ????, port:%d, buffer:%d", __func__, port_index, buffer_index);
        pthread_mutex_unlock(&buffer_lock_);
        return -1;
    }
    NDLLOG(LOGTAG, LOG_BUFFER_STATUS, "writeToFreeBuffer (port : %d, buffer index : %d, ts : %d)",
            port_index, buffer_index, buf->nTimeStamp.nLowPart);

    OMX_BUFFERHEADERTYPE* owners_buf = buffer_owner->getBuffer(
            buffer_owner_port_index,
            buffer_index);
    if(!owners_buf)
    {
        pthread_mutex_unlock(&buffer_lock_);
        NDLLOG(LOGTAG, NDL_LOGE, "%s:buffer full .......", __func__);
        return -1;
    }

    buf->nOffset = 0;
    buf->nFilledLen = owners_buf->nFilledLen;
    buf->nTimeStamp = owners_buf->nTimeStamp;
    buf->nFlags = owners_buf->nFlags;
    if (emptyBuffer(port_index, buffer_index)==OMX_ErrorNone)
    {
        pthread_mutex_unlock(&buffer_lock_);
        return buf->nFilledLen;
    }
    pthread_mutex_unlock(&buffer_lock_);
    return 0;
}


namespace {
    /// For Debugging Convenience
    std::map<int, std::string> omxStateToString = {
        {OMX_StateInvalid,           "OMX_StateInvalid"},
        {OMX_StateLoaded,            "OMX_StateLoaded"},
        {OMX_StateIdle,              "OMX_StateIdle"},
        {OMX_StateExecuting,         "OMX_StateExecuting"},
        {OMX_StatePause,             "OMX_StatePause"},
        {OMX_StateWaitForResources,  "OMX_StateWaitForResources"},
        {OMX_StateKhronosExtensions, "OMX_StateKhronosExtensions"},
        {OMX_StateVendorStartUnused, "OMX_StateVendorStartUnused"},
        {OMX_StateMax,               "OMX_StateMax"}
    };
}

OMX_ERRORTYPE OmxClient::eventHandler(OMX_EVENTTYPE event,
        OMX_U32 data1,
        OMX_U32 data2,
        OMX_PTR eventdata)
{
    NDLLOG(LOGTAG,LOG_CALLBACK,
            "%s  event 0x%x : data1 : 0x%x(%d), data2 : 0x%x(%d) ",
            __func__, event, (int)data1, (int)data1, (int)data2, (int)data2);
    unsigned int evt = event;
    switch (evt)
    {
        case OMX_EventError:
            NDLLOG(LOGTAG, NDL_LOGE, "Error event detected : 0x%x, 0x%x",
                    (uint32_t)data1, (uint32_t)data2);
            if (data1 == (OMX_U32)OMX_ErrorUnderflow)
                player_listener_(OMX_CLIENT_EVT_UNDERFLOW,0, 0, 0, userdata_);
            else if (data1 == (OMX_U32)OMX_ErrorSameState)
                NDLLOG(LOGTAG,NDL_LOGI, "OMX_ErrorSameState set to (%s) ", omxStateToString[(int)data2].c_str());
            else if (data1 == (OMX_U32)OMX_ErrorInsufficientResources)
                NDLLOG(LOGTAG,NDL_LOGI, "OMX_ErrorinsufficientResource");
            break;
        case OMX_EventCmdComplete :
            if(data1==OMX_CommandStateSet)
            {
                NDLLOG(LOGTAG,NDL_LOGI, "client state set to 0x%x(%s) ",
                        (uint32_t)data2, omxStateToString[(int)data2].c_str());
                pthread_cond_signal(&state_cond_);
            }
            else if (data1 == OMX_CommandFlush)
            {
                NDLLOG(LOGTAG, NDL_LOGI, "flush done %d port ", (uint32_t)data2);

                // erase flush port
                auto iter = std::find(flushing_.begin(), flushing_.end(), (int)data2);
                if (iter != flushing_.end()) {
                    flushing_.erase(iter);
                    NDLLOG(LOGTAG, NDL_LOGI, "erase list success! %d port ", (uint32_t)data2);
                } else {
                    NDLLOG(LOGTAG, NDL_LOGI, "erase list fail! check %d port ", (uint32_t)data2);
                }

                if (flushing_.empty())
                    pthread_cond_signal(&state_cond_);
            }
            else if (data1 == OMX_CommandPortEnable)
            {
                NDLLOG(LOGTAG,NDL_LOGI, "port %u enabled", (unsigned int) data2);
                setPortMapState((int)data2, true);
                pthread_cond_signal(&state_cond_);
            }
            else if (data1 == OMX_CommandPortDisable)
            {
                NDLLOG(LOGTAG,NDL_LOGI, "port %u disabled", (unsigned int) data2);
                setPortMapState((int)data2, false);
                pthread_cond_signal(&state_cond_);
            }
            break;
        case OMX_EventPortSettingsChanged :
            NDLLOG(LOGTAG,NDL_LOGI, "%dth port setting changed", (int)data1);
            player_listener_(OMX_CLIENT_EVT_PORT_SETTING_CHANGED,
                    data1, 0 , 0, userdata_);
            break;
        case OMX_EventPortFormatDetected:
            NDLLOG(LOGTAG,NDL_LOGI, "%dth port format detected", (int)data1);
            break;
        case OMX_EventBufferFlag:
            NDLLOG(LOGTAG, NDL_LOGI, "end-of-stream detected...");
            player_listener_(OMX_CLIENT_EVT_END_OF_STREAM, 0, 0, 0, userdata_);
            break;
        case OMX_EventResourcesAcquired:
            NDLLOG(LOGTAG, NDL_LOGI, "Resource Acquired");
            player_listener_(OMX_CLIENT_EVT_RESOURCE_ACQUIRED, 0, 0, 0, userdata_);
            break;
    }
    //TODO check if it is needed filtering. Send every vender specific event, now
    if (event>=OMX_EventVendorStartUnused)
        player_listener_(event, data1, data2, eventdata, userdata_);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OmxClient::emptyBufferDone(OMX_BUFFERHEADERTYPE* buf)
{
    // notify esplayer buf->port_index, buf->buffer_index
    // TODO: too many logs here!!!! (debug level)
    BufferInfo* info = (BufferInfo*)buf->pAppPrivate;
    NDLLOG(LOGTAG, LOG_CALLBACK, "%s (%p) -- (port : %d, buffer index : %d, ts : %d)", __func__, buf, info->port_index, info->buffer_index, buf->nTimeStamp.nLowPart);

    if (info)
    {
        // buffer is owen by CLIENT that means it is already returned with BufferDone event
        if (isFreeBufferIndex(info->port_index, info->buffer_index)) {
            NDLLOG(LOGTAG, NDL_LOGE, "Buffer is already owned by CLIENT : emptyBufferDone (port : %d, buffer index : %d",
                    info->port_index, info->buffer_index);
            return OMX_ErrorUndefined;
        }

        //TODO check whether another state is needed to be add
        if (getPortMapState(info->port_index) &&
              (current_state_==OMX_StateExecuting || current_state_ == OMX_StatePause))
        {
            NDLLOG(LOGTAG, LOG_BUFFER, "emptyBufferDone (port : %d, buffer index : %d, ts : %lld, high:%d, low:%d)",
                    info->port_index, info->buffer_index, from_omx_time(buf->nTimeStamp), buf->nTimeStamp.nHighPart, buf->nTimeStamp.nLowPart);
            player_listener_(OMX_CLIENT_EVT_EMPTY_BUFFER_DONE,
                    info->port_index, buf->nTimeStamp.nLowPart, 0, userdata_);
        } else {
            NDLLOG(LOGTAG, NDL_LOGI, "stop feeding... ");
        }
        setBufferStatus(buf, BUFFER_STATUS_OWNED_BY_CLIENT);
        pthread_cond_signal(&buffer_cond_);
    } else {
        NDLLOG(LOGTAG, NDL_LOGE, "no buffer info in empty buffer done buffer.... ");
    }
    return OMX_ErrorNone;
}


OMX_ERRORTYPE OmxClient::fillBufferDone(OMX_BUFFERHEADERTYPE* buf)
{
    // notify esplayer buf->port_index, buf->buffer_index
    NDLLOG(LOGTAG, LOG_CALLBACK, "%s (%p) --", __func__, buf);
    BufferInfo* info = (BufferInfo*)buf->pAppPrivate;
    if (info)
    {
        // buffer is owen by CLIENT that means it is already returned with BufferDone event
        if (isFreeBufferIndex(info->port_index, info->buffer_index)) {
            NDLLOG(LOGTAG, NDL_LOGE, "Buffer is already owned by CLIENT : fillBufferDone (port : %d, buffer index : %d",
                    info->port_index, info->buffer_index);
            return OMX_ErrorUndefined;
        }
        setBufferStatus(buf, BUFFER_STATUS_OWNED_BY_CLIENT);
        pthread_cond_signal(&buffer_cond_);
        //TODO check whether another state is needed to be add
        if (getPortMapState(info->port_index) &&
              (current_state_==OMX_StateExecuting || current_state_ == OMX_StatePause))
        {
            NDLLOG(LOGTAG, LOG_BUFFER, "fillBufferDone (port : %d, buffer index : %d, ts : %lld)",
                    info->port_index, info->buffer_index, from_omx_time(buf->nTimeStamp));
            player_listener_(OMX_CLIENT_EVT_FILL_BUFFER_DONE,
                    info->port_index, info->buffer_index, 0, userdata_);
        } else {
            NDLLOG(LOGTAG, NDL_LOGI, "stop request output");
        }
        pthread_cond_signal(&buffer_cond_);
    } else {
        NDLLOG(LOGTAG, NDL_LOGE, "no buffer info in fill buffer done buffer.... ");
    }
    return OMX_ErrorNone;
}

bool OmxClient::insertPortMap(int portIdx) {
    std::pair<std::map<int, bool>::iterator, bool> pr;
    // set the state for port index. initial setting is false.
    pr = enabled_port_map_.insert(std::pair<int, bool>(portIdx, false));
    if (!pr.second) return false;
    else            return true;
}

bool OmxClient::getPortMapState(int portIdx) {
    bool res = enabled_port_map_.at(portIdx);
    return res;
}

bool OmxClient::setPortMapState(int portIdx, bool state) {
    auto iter = enabled_port_map_.find(portIdx);
    if (iter != enabled_port_map_.end()) {
        enabled_port_map_[portIdx] = state;
        NDLLOG(LOGTAG, NDL_LOGD, "setPortMapState success! portIdx: %d, state: %s",
                portIdx, enabled_port_map_[portIdx] == true ? "true" : "false");
        return true;
    }
    NDLLOG(LOGTAG, NDL_LOGD, "setPortMapState fail! portIdx: %d, state: %s",
            portIdx, state == true ? "true" : "false");
    return false;
}

void OmxClient::printPortMap() const {
    for (auto& iter: enabled_port_map_)
        NDLLOG(LOGTAG, NDL_LOGD, "PortNum: %d, state: %s",
                iter.first, iter.second == true ? "true" : "false");
}
