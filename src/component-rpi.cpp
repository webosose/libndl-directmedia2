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

#include "component.h"

#include <assert.h>

#include <OMX_Types.h>

using namespace NDL_Esplayer;

#define LOGTAG "component-rpi"
#include "debug.h"

#define  PORT_VIDEO_CODEC_INPUT 130
#define  PORT_VIDEO_CODEC_OUTPUT 131
#define  PORT_VIDEO_RENDERER_VIDEO_INPUT 90

#define  PORT_VIDEO_SCHEDULER_CLOCK_INPUT 12
#define  PORT_VIDEO_SCHEDULER_INPUT  10
#define  PORT_VIDEO_SCHEDULER_OUTPUT 11

#define  PORT_AUDIO_CODEC_INPUT 120
#define  PORT_AUDIO_CODEC_OUTPUT 121

#if SUPPORT_ALSA_RENDERER_COMPONENT
#define  PORT_AUDIO_RENDERER_AUDIO_INPUT 0
#define  PORT_AUDIO_RENDERER_CLOCK_INPUT 1
#else
#define  PORT_AUDIO_RENDERER_AUDIO_INPUT 100
#define  PORT_AUDIO_RENDERER_CLOCK_INPUT  101
#endif

#define  PORT_AUDIO_MIXER_INPUT 232
#define  PORT_AUDIO_MIXER_OUTPUT 231
#define  PORT_AUDIO_MIXER_CLOCK_INPUT 230

#define  VIDEO_IN_BUFFER_COUNT 60
#define  VIDEO_OUT_BUFFER_COUNT 4
#define  VIDEO_IN_BUFFER_SIZE 81920 //(2*1920*1080)
#define  VIDEO_OUT_BUFFER_SIZE   4096
#define  VIDEO_RENDER_INPUT_SIZE 4096

#define  AUDIO_IN_BUFFER_COUNT 16
#define  AUDIO_OUT_BUFFER_COUNT 16
#define  AUDIO_IN_BUFFER_SIZE  65536
#define  AUDIO_OUT_BUFFER_SIZE 20*(64*1024)

#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)
#define AUDIO_BUFFER_SECONDS 3

#define AUDIO_RENDER_IN_BUFFER_COUNT 16
#define AUDIO_RENDER_IN_BUFFER_SIZE 4096

Component::Component(ComponentCallback callback)
    : OmxClient([] (int event, uint32_t data1, uint32_t data2, void* data, void* userdata) {
            ((Component*)userdata)->onOmxClientCallback(event, data1, data2, data);
            }, this)
, callback_(callback)
{
}

Component::~Component() {
}

int Component::create(COMPONENT component)
{
    OMX_PORT_PARAM_TYPE port_param;
    omx_init_structure(&port_param, OMX_PORT_PARAM_TYPE);
    int error = OMX_ErrorNone;
    std::string name = "";

    switch(component) {
        case CLOCK:
            name = "OMX.broadcom.clock";
            RETURN_FAIL_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.broadcom.clock \n");
            error = getParam(OMX_IndexParamOtherInit,&port_param);
            if (error != OMX_ErrorNone) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : %s getParam error!", __func__, name.c_str());
            }
            break;
        case VIDEO_RENDERER:
            name = "OMX.drm.video_render";
            RETURN_FAIL_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.drm.video_render");
            error = getParam(OMX_IndexParamVideoInit,&port_param);
            if (error != OMX_ErrorNone) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : %s getParam error!", __func__, name.c_str());
            }
            break;

        case VIDEO_SCHEDEULER:
            name = "OMX.broadcom.video_scheduler";
            RETURN_FAIL_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.broadcom.video_scheduler");
            error = getParam(OMX_IndexParamVideoInit,&port_param);
            if (error != OMX_ErrorNone) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : %s getParam error!", __func__, name.c_str());
            }
            port_param.nPorts = port_param.nPorts+1;
            port_index_clock_input_ = PORT_VIDEO_SCHEDULER_CLOCK_INPUT;
            break;

        case AUDIO_MIXER:
            name = "OMX.broadcom.audio_mixer";
            RETURN_FAIL_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.broadcom.audio_mixer");
            error = getParam(OMX_IndexParamAudioInit,&port_param);
            if (error != OMX_ErrorNone) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : %s getParam error!", __func__, name.c_str());
            }
            break;

        case AUDIO_RENDERER:
#if SUPPORT_ALSA_RENDERER_COMPONENT
            name = "OMX.alsa.audio_render";
            RETURN_FAIL_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.alsa.audio_render");
#else
            name = "OMX.broadcom.audio_render";
            RETURN_FAIL_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.broadcom.audio_render");
#endif
            error = getParam(OMX_IndexParamAudioInit,&port_param);
            if (error != OMX_ErrorNone) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : %s getParam error!", __func__, name.c_str());
            }
            /* input port setting*/
            OMX_PARAM_PORTDEFINITIONTYPE port_def;
            omx_init_structure(&port_def, OMX_PARAM_PORTDEFINITIONTYPE);
            port_def.nPortIndex = port_param.nStartPortNumber;
            BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def), "get parameter >> audio render Input port %d", port_def.nPortIndex);
            NDLLOG(LOGTAG, NDL_LOGI, "port index:%d, count:%d, size:%d", port_def.nPortIndex, port_def.nBufferCountActual, port_def.nBufferSize);
            port_def.nPortIndex = port_param.nStartPortNumber+1;
            BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def), "get parameter >> audio render Output port %d", port_def.nPortIndex);
            NDLLOG(LOGTAG, NDL_LOGI, "port index:%d, count:%d, size:%d", port_def.nPortIndex, port_def.nBufferCountActual, port_def.nBufferSize);

            port_index_input_ = PORT_AUDIO_RENDERER_AUDIO_INPUT;
            port_index_clock_input_ = PORT_AUDIO_RENDERER_CLOCK_INPUT;
            port_index_output_ = -1;
            input_buffer_count_ = AUDIO_RENDER_IN_BUFFER_COUNT;
            input_buffer_size_ = AUDIO_RENDER_IN_BUFFER_SIZE;

            //temp. force add 1 port
            port_param.nPorts+=1;
            break;
        default:
            return NDL_ESP_RESULT_FAIL;
    }

    NDLLOG(LOGTAG, NDL_LOGD, "Disable all %d ports(start:%d) of %s", port_param.nPorts, port_param.nStartPortNumber, name.c_str());
    for(uint32_t i = 0; i < port_param.nPorts; i++) {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        omx_init_structure(&portFormat, OMX_PARAM_PORTDEFINITIONTYPE);
        portFormat.nPortIndex = port_param.nStartPortNumber+i;
        error = getParam(OMX_IndexParamPortDefinition, &portFormat);
        if(error != OMX_ErrorNone)
        {
            if(portFormat.bEnabled == OMX_FALSE)
                continue;
        }

        // set the portNumber to Map structure
        insertPortMap((int)(port_param.nStartPortNumber + i));
        NDLLOG(LOGTAG, NDL_LOGD, "[Default port(%d) Info. count:%d, size:%d]", portFormat.nPortIndex, portFormat.nBufferCountActual, portFormat.nBufferSize);
        error = sendCommand(OMX_CommandPortDisable, port_param.nStartPortNumber+i);
        if(error != OMX_ErrorNone) {
            NDLLOG(LOGTAG, NDL_LOGE, "Error disable port %d component name : %s omx_err(0x%08x) \n",  (int)(port_param.nStartPortNumber) + i, name.c_str(), (int)error);
        }
        else {
            NDLLOG(LOGTAG, NDL_LOGD, "disable port %d component name : %s \n",  (int)(port_param.nStartPortNumber) + i, name.c_str());
        }
    }

    port_index_input_  = port_param.nStartPortNumber;
    if( component != AUDIO_RENDERER ) {
        port_index_output_ = port_index_input_+1;

        if (port_index_output_ > (int)(port_param.nStartPortNumber+port_param.nPorts-1))
            port_index_output_ = port_param.nStartPortNumber+port_param.nPorts-1;
    }

    if( component == AUDIO_MIXER ) {
        port_index_input_ = PORT_AUDIO_MIXER_INPUT;
        port_index_output_ = PORT_AUDIO_MIXER_OUTPUT;
        port_index_clock_input_ = PORT_AUDIO_MIXER_CLOCK_INPUT;
    }
    NDLLOG(LOGTAG, NDL_LOGI, "%s: input port:%d, output port:%d, clock_in:%d", name.c_str(), port_index_input_, port_index_output_, port_index_clock_input_);

    return NDL_ESP_RESULT_SUCCESS;
}

int Component::create(NDL_ESP_VIDEO_CODEC codec)
{
    OMX_PORT_PARAM_TYPE port_param;
    omx_init_structure(&port_param, OMX_PORT_PARAM_TYPE);
    int error = OMX_ErrorNone;
    std::string name;

    switch(codec) {
        case NDL_ESP_VIDEO_CODEC_H262:
        case NDL_ESP_VIDEO_CODEC_H264:
            name = "OMX.broadcom.video_decode";
            BREAK_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.broadcom.video_decode");
            error = getParam(OMX_IndexParamVideoInit,&port_param);
            if (error != OMX_ErrorNone) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : %s getParam error!", __func__, name.c_str());
            }
            break;
        case NDL_ESP_VIDEO_CODEC_H265:
            break;
        default:
            return NDL_ESP_RESULT_SUCCESS;
    }

    /* Disable all buffers */
    for(uint32_t i = 0; i < port_param.nPorts; i++) {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        omx_init_structure(&portFormat, OMX_PARAM_PORTDEFINITIONTYPE);
        portFormat.nPortIndex = port_param.nStartPortNumber+i;
        error = getParam(OMX_IndexParamPortDefinition, &portFormat);
        if(error != OMX_ErrorNone)
        {
            if(portFormat.bEnabled == OMX_FALSE)
                continue;
        }

        // set the portNumber to Map structure
        insertPortMap((int)(port_param.nStartPortNumber + i));

        error = sendCommand(OMX_CommandPortDisable, port_param.nStartPortNumber+i);
        if(error != OMX_ErrorNone) {
            NDLLOG(LOGTAG, NDL_LOGE, "Error disable port %d component name : %s omx_err(0x%08x) \n",  (int)(port_param.nStartPortNumber) + i, name.c_str(), (int)error);
        }
        else {
            NDLLOG(LOGTAG, NDL_LOGD, "disable port %d component name : %s \n",  (int)(port_param.nStartPortNumber) + i, name.c_str());
        }
    }

    port_index_input_  = port_param.nStartPortNumber;
    port_index_output_ = port_index_input_+1;

    if (port_index_output_ > (int)(port_param.nStartPortNumber+port_param.nPorts-1))
        port_index_output_ = port_param.nStartPortNumber+port_param.nPorts-1;

    input_buffer_count_  = VIDEO_IN_BUFFER_COUNT;
    output_buffer_count_ = VIDEO_OUT_BUFFER_COUNT;
    input_buffer_size_   = VIDEO_IN_BUFFER_SIZE;
    output_buffer_size_  = VIDEO_OUT_BUFFER_SIZE;

    return NDL_ESP_RESULT_SUCCESS;
}

int Component::create(NDL_ESP_AUDIO_CODEC codec)
{
    OMX_PORT_PARAM_TYPE port_param;
    omx_init_structure(&port_param, OMX_PORT_PARAM_TYPE);
    int error = OMX_ErrorNone;
    std::string name;
    int ret = NDL_ESP_RESULT_FAIL;
    switch(codec) {
        case NDL_ESP_AUDIO_CODEC_MP2:
        case NDL_ESP_AUDIO_CODEC_MP3:
        case NDL_ESP_AUDIO_CODEC_AC3:
        case NDL_ESP_AUDIO_CODEC_EAC3:
        case NDL_ESP_AUDIO_CODEC_AAC:
        case NDL_ESP_AUDIO_CODEC_HEAAC:
        case NDL_ESP_AUDIO_CODEC_PCM_44100_2CH:
        case NDL_ESP_AUDIO_CODEC_PCM_48000_2CH:
            name = "OMX.broadcom.audio_decode";
            BREAK_IF_NONZERO(createByName((const std::string)(name)), "create component >> OMX.broadcom.audio_decode");
            error = getParam(OMX_IndexParamAudioInit,&port_param);
            if (error != OMX_ErrorNone) {
                NDLLOG(LOGTAG, NDL_LOGE, "%s : %s getParam error!", __func__, name.c_str());
            }
            break;
        default:
            return ret;
    }

    /* Disable all buffers */
    for(uint32_t i = 0; i < port_param.nPorts; i++) {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        omx_init_structure(&portFormat, OMX_PARAM_PORTDEFINITIONTYPE);
        portFormat.nPortIndex = port_param.nStartPortNumber+i;
        error = getParam(OMX_IndexParamPortDefinition, &portFormat);
        NDLLOG(LOGTAG, NDL_LOGI, "audio codec port index:%d, count:%d, size:%d", portFormat.nPortIndex, portFormat.nBufferCountActual, portFormat.nBufferSize);
        if(error != OMX_ErrorNone)
        {
            if(portFormat.bEnabled == OMX_FALSE)
                continue;
        }

        // set the portNumber to Map structure
        insertPortMap((int)(port_param.nStartPortNumber + i));

        error = sendCommand(OMX_CommandPortDisable, port_param.nStartPortNumber+i);
        if(error != OMX_ErrorNone) {
            NDLLOG(LOGTAG, NDL_LOGE, "Error disable port %d component name : %s omx_err(0x%08x) \n",  (int)(port_param.nStartPortNumber) + i, name.c_str(), (int)error);
        }
        else {
            NDLLOG(LOGTAG, NDL_LOGD, "disable port %d component name : %s \n",  (int)(port_param.nStartPortNumber) + i, name.c_str());
        }
    }

    port_index_input_  = port_param.nStartPortNumber;
    port_index_output_ = port_index_input_+1;

    if (port_index_output_ > (int)(port_param.nStartPortNumber+port_param.nPorts-1))
        port_index_output_ = port_param.nStartPortNumber+port_param.nPorts-1;

    OMX_CONFIG_BOOLEANTYPE boolType;
    omx_init_structure(&boolType, OMX_CONFIG_BOOLEANTYPE);
#if SUPPORT_AUDIOMIXER
    boolType.bEnabled    = OMX_FALSE;
#else
    boolType.bEnabled    = OMX_TRUE;
#endif
    int32_t omx_ret = setParam(OMX_IndexParamBrcmDecoderPassThrough, &boolType);
    NDLLOG(LOGTAG, NDL_LOGI, "set OMX_IndexParamBrcmDecoderPassThrough ret:%d", omx_ret);

    input_buffer_count_  = AUDIO_IN_BUFFER_COUNT;
    input_buffer_size_   = AUDIO_IN_BUFFER_SIZE;

    output_buffer_count_ = AUDIO_OUT_BUFFER_COUNT;
    output_buffer_size_  = AUDIO_DECODE_OUTPUT_BUFFER;

    ret = NDL_ESP_RESULT_SUCCESS;
    return ret;
}

void Component::onOmxClientCallback(int event, uint32_t data1, uint32_t data2, void* data)
{
    callback_(event, data1, data2, data);
}

int Component::loadInitialPortSetting()
{
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    omx_init_structure(&port_def, OMX_PARAM_PORTDEFINITIONTYPE);
    int ret = NDL_ESP_RESULT_FAIL;
    do {
        //Load initial setting for input port
        port_def.nPortIndex = port_index_input_;
        BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def),
                "get port def >> %d th", port_index_input_);

        input_buffer_count_=port_def.nBufferCountActual;
        input_buffer_size_=port_def.nBufferSize;

        //Load initial setting for output  port
        port_def.nPortIndex = port_index_output_;
        BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def),
                "get port def >> %d th", port_index_output_);
        output_buffer_count_=port_def.nBufferCountActual;
        output_buffer_size_=port_def.nBufferSize;

        ret = NDL_ESP_RESULT_SUCCESS;
    } while(0);
    return ret;
}

//Not support in RPI
int Component::setResourceInfo(int audioPort, int videoPort, int mixerPort, int coreType)
{
    return NDL_ESP_RESULT_SUCCESS;
}

int Component::setRenderFormat(int video_width, int video_height)
{
    OMX_VIDEO_PORTDEFINITIONTYPE port_config;
    port_config.nFrameWidth  = video_width;
    port_config.nFrameHeight = video_height;
    port_config.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
    RETURN_FAIL_IF_NONZERO(setConfig(OMX_IndexConfigDisplayRegion, &port_config), "setting video render parameter");
    return NDL_ESP_RESULT_SUCCESS;
}

int Component::setVideoFormat(NDL_ESP_META_DATA *metadata)
{
    OMX_VIDEO_CODINGTYPE eCompressionFormat;
    int ret =  NDL_ESP_RESULT_FAIL;
    switch(metadata->video_codec) {
        case NDL_ESP_VIDEO_CODEC_H262:
            eCompressionFormat = OMX_VIDEO_CodingMPEG2;
            break;
        case NDL_ESP_VIDEO_CODEC_H264:
            eCompressionFormat = OMX_VIDEO_CodingAVC;
            break;
        default:
            return ret;
    }
    do {
        OMX_VIDEO_PARAM_PORTFORMATTYPE format;
        omx_init_structure(&format, OMX_VIDEO_PARAM_PORTFORMATTYPE);
        format.eCompressionFormat = eCompressionFormat;
        format.xFramerate = 1966080;//metadata->framerate; ????
        format.nPortIndex = port_index_input_;
        BREAK_IF_NONZERO(setParam(OMX_IndexParamVideoPortFormat, &format), "set parameter of video input port def");

        OMX_PARAM_PORTDEFINITIONTYPE port_def;
        omx_init_structure(&port_def, OMX_PARAM_PORTDEFINITIONTYPE);
        port_def.nPortIndex = port_index_input_;
        BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def), "get parameter of video input port");

        port_def.nPortIndex = port_index_input_;
        port_def.nBufferCountActual = 60;
        port_def.format.video.nFrameWidth = metadata->width;
        port_def.format.video.nFrameHeight = metadata->height;
        BREAK_IF_NONZERO(setParam(OMX_IndexParamPortDefinition, &port_def), "set parameter of video input port def");
        ret = NDL_ESP_RESULT_SUCCESS;
    } while (0);

    return ret;
}

int Component::setAudioCodecFormat(int port_index, NDL_ESP_META_DATA *metadata)
{
    int ret = NDL_ESP_RESULT_FAIL;
    OMX_AUDIO_CODINGTYPE eEncoding;
    //OMX_AUDIO_MP3STREAMFORMATTYPE mp3format = OMX_AUDIO_MP3StreamFormatMP2Layer3;

    switch(metadata->audio_codec) {
        case NDL_ESP_AUDIO_CODEC_MP2:
            eEncoding = OMX_AUDIO_CodingMP3;
            //mp3format = OMX_AUDIO_MP3StreamFormatMP2Layer3;
            break;
        case NDL_ESP_AUDIO_CODEC_MP3:
            eEncoding = OMX_AUDIO_CodingMP3;
            //mp3format = OMX_AUDIO_MP3StreamFormatMP1Layer3;
            break;
        case NDL_ESP_AUDIO_CODEC_AAC:
        case NDL_ESP_AUDIO_CODEC_HEAAC:
            eEncoding = OMX_AUDIO_CodingAAC;
            break;
        case NDL_ESP_AUDIO_CODEC_PCM_44100_2CH:
            eEncoding = OMX_AUDIO_CodingPCM;
            break;
        case NDL_ESP_AUDIO_CODEC_PCM_48000_2CH:
            eEncoding = OMX_AUDIO_CodingPCM;
            break;
        default:
            NDLLOG(LOGTAG, NDL_LOGE, "no format %d\n", metadata->audio_codec);
            return ret;
    }

    do {
        OMX_CONFIG_BOOLEANTYPE boolType;
        omx_init_structure(&boolType, OMX_CONFIG_BOOLEANTYPE);
        boolType.bEnabled = OMX_TRUE;
        NDLLOG(LOGTAG, NDL_LOGD, "%s passthrough boolType:%d ",__func__, boolType.bEnabled);
        BREAK_IF_NONZERO(setParam(OMX_IndexParamBrcmDecoderPassThrough, &boolType), "set parameter >> audio decoder Input port %d", port_index);

        /* input port setting*/
        OMX_PARAM_PORTDEFINITIONTYPE port_def;
        omx_init_structure(&port_def, OMX_PARAM_PORTDEFINITIONTYPE);
        port_def.nPortIndex = port_index;
        BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def), "get parameter >> audio decoder Input port %d", port_index);
        port_def.format.audio.eEncoding = eEncoding;
        port_def.nBufferSize = AUDIO_DECODE_OUTPUT_BUFFER * (metadata->channels * metadata->bitspersample) >> (rounded_up_channels_shift[metadata->channels] + 4);
        port_def.nBufferCountActual = std::max(port_def.nBufferCountMin, 16U);
        NDLLOG(LOGTAG, NDL_LOGD, "audio codec input nBufferSize:%d, nBufferCountActual:%d", port_def.nBufferSize, port_def.nBufferCountActual);
        BREAK_IF_NONZERO(setParam(OMX_IndexParamPortDefinition, &port_def), "set parameter >> audio decoder Input port %d", port_index);

        /*output port setting*/
        omx_init_structure(&port_def, OMX_PARAM_PORTDEFINITIONTYPE);
        port_def.nPortIndex = port_index + 1;
        BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def), "get parameter >> audio decoder Output port %d", port_index + 1);
        unsigned int  m_BytesPerSec  = metadata->samplerate * 2 << rounded_up_channels_shift[metadata->channels];
        unsigned int m_BufferLen     = m_BytesPerSec * AUDIO_BUFFER_SECONDS;
        port_def.nBufferCountActual = std::max((unsigned int)port_def.nBufferCountMin, m_BufferLen / port_def.nBufferSize);
        NDLLOG(LOGTAG, NDL_LOGD, "audio codec output nBufferSize:%d, nBufferCountActual:%d", port_def.nBufferSize, port_def.nBufferCountActual);
        BREAK_IF_NONZERO(setParam(OMX_IndexParamPortDefinition, &port_def), "set parameter >> audio decoder Output port %d", port_index + 1);

        /* input port setting for audio port format */
        OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
        omx_init_structure(&formatType, OMX_AUDIO_PARAM_PORTFORMATTYPE);
        formatType.nPortIndex = port_index;
        formatType.eEncoding = eEncoding;
        BREAK_IF_NONZERO(setParam(OMX_IndexParamAudioPortFormat, &formatType), "set parameter >> audio decoder input port format %d", port_index);
        ret = NDL_ESP_RESULT_SUCCESS;
    } while(0);
    return ret;
}

int Component::setAudioFormat(int port_index, NDL_ESP_AUDIO_CODEC codec)
{
    OMX_AUDIO_MP3STREAMFORMATTYPE mp3format = OMX_AUDIO_MP3StreamFormatMP2Layer3;
    OMX_U32 samplerate = 48000;
    int ret = NDL_ESP_RESULT_FAIL;

    switch(codec) {
        case NDL_ESP_AUDIO_CODEC_MP2:
            mp3format = OMX_AUDIO_MP3StreamFormatMP2Layer3;
            break;
        case NDL_ESP_AUDIO_CODEC_MP3:
            mp3format = OMX_AUDIO_MP3StreamFormatMP1Layer3;
            break;
        case NDL_ESP_AUDIO_CODEC_AAC:
        case NDL_ESP_AUDIO_CODEC_HEAAC:
            break;
        case NDL_ESP_AUDIO_CODEC_PCM_44100_2CH:
            samplerate = 44100;
            break;
        case NDL_ESP_AUDIO_CODEC_PCM_48000_2CH:
            samplerate = 48000;
            break;
        default:
            NDLLOG(LOGTAG, NDL_LOGE, "no format %d\n", codec);
            return ret;
    }

    do {
        OMX_PARAM_PORTDEFINITIONTYPE port_def;
        port_def.nSize = sizeof(port_def);
        port_def.nVersion.nVersion = OMX_VERSION;
        port_def.nPortIndex = port_index;

        BREAK_IF_NONZERO(getParam(OMX_IndexParamPortDefinition, &port_def),
                "get parameter >> audio decoder port %d", port_index);

        port_def.eDomain=OMX_PortDomainAudio;
        port_def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
        BREAK_IF_NONZERO(setParam(OMX_IndexParamPortDefinition, &port_def),
                "set parameter >> audio decoder port %d", port_index);

        switch(codec) {
            default:
                NDLLOG(LOGTAG, NDL_LOGE, "%s : unknown codec:%d", __func__, codec);
                break;
            case NDL_ESP_AUDIO_NONE:
                break;
            case NDL_ESP_AUDIO_CODEC_MP2:
            case NDL_ESP_AUDIO_CODEC_MP3:
                {
                    OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
                    omx_init_structure(&formatType, OMX_AUDIO_PARAM_PORTFORMATTYPE);
                    formatType.nPortIndex = port_index;
                    formatType.eEncoding  = OMX_AUDIO_CodingPCM;
                    samplerate = 44100;
                    LOG_IF_NONZERO(setParam(OMX_IndexParamAudioPortFormat, &formatType),
                            "set parameter >> OMX_IndexParamAudioPortFormat aac decoder port %d", port_index);
                    break;

                    OMX_AUDIO_PARAM_MP3TYPE mp3;
                    mp3.nSize = sizeof(OMX_AUDIO_PARAM_MP3TYPE);
                    mp3.nVersion.nVersion = OMX_VERSION;
                    mp3.nPortIndex = port_index;
                    LOG_IF_NONZERO(getParam(OMX_IndexParamAudioMp3, &mp3),
                            "get parameter >> mp3 decoder port %d", port_index);
                    mp3.nChannels = 2;
                    mp3.nBitRate = 0;
                    mp3.nSampleRate = samplerate;
                    mp3.nPortIndex = port_index;
                    mp3.eChannelMode = OMX_AUDIO_ChannelModeStereo;
                    mp3.eFormat = mp3format;
                    RETURN_FAIL_IF_NONZERO(setParam(OMX_IndexParamAudioMp3, &mp3),
                            "set paramter >> mp3 decoder port %d", port_index);
                }
                break;
            case NDL_ESP_AUDIO_CODEC_AC3:
            case NDL_ESP_AUDIO_CODEC_EAC3:
                break;
            case NDL_ESP_AUDIO_CODEC_AAC:
            case NDL_ESP_AUDIO_CODEC_HEAAC:
                {
                    // formatType.eEncoding = OMX_AUDIO_CodingAAC;
                    OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
                    omx_init_structure(&formatType, OMX_AUDIO_PARAM_PORTFORMATTYPE);
                    formatType.nPortIndex = port_index;
                    formatType.eEncoding =  OMX_AUDIO_CodingPCM;
                    samplerate = 44100;
                    LOG_IF_NONZERO(setParam(OMX_IndexParamAudioPortFormat, &formatType),
                            "set parameter >> OMX_IndexParamAudioPortFormat  aac decoder port %d", port_index);
                    break;
                    OMX_AUDIO_PARAM_AACPROFILETYPE aac;
                    aac.nSize = sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE);
                    aac.nVersion.nVersion = OMX_VERSION;
                    aac.nPortIndex = port_index;
                    LOG_IF_NONZERO(getParam(OMX_IndexParamAudioAac, &aac),
                            "get parameter >>  aac decoder port %d", port_index);

                    aac.nChannels = 2;
                    aac.nSampleRate = samplerate;
                    aac.nBitRate = 0;
                    aac.nAudioBandWidth = 0;
                    aac.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4ADTS;
                    aac.eChannelMode = OMX_AUDIO_ChannelModeStereo;
                    RETURN_FAIL_IF_NONZERO(setParam(OMX_IndexParamAudioAac, &aac),
                            "set paramter >> aac decoder port %d", port_index);
                }
                break;
            case NDL_ESP_AUDIO_CODEC_PCM_44100_2CH:
            case NDL_ESP_AUDIO_CODEC_PCM_48000_2CH:
                {
                    OMX_AUDIO_PARAM_PCMMODETYPE pcm;
                    pcm.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
                    pcm.nVersion.nVersion = OMX_VERSION;
                    pcm.nPortIndex = port_index;
                    pcm.nSamplingRate = samplerate;
                    pcm.nChannels = 2;
                    pcm.nBitPerSample = 16;//TODO: Needo to find way to get this value.
                    pcm.eEndian = OMX_EndianLittle;
                    pcm.eNumData = OMX_NumericalDataSigned;
                    pcm.bInterleaved = OMX_TRUE;
                    pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;
                    RETURN_FAIL_IF_NONZERO(setDataFormat(pcm), "set data format of pcm >>  port_index : %d" , port_index);
                }
                break;
        }
        ret = NDL_ESP_RESULT_SUCCESS;
    } while(0);
    return ret;
}

int Component::setDataFormat(OMX_AUDIO_PARAM_PCMMODETYPE data)
{
    OMX_AUDIO_PARAM_PCMMODETYPE pcm;
    int ret = NDL_ESP_RESULT_FAIL;

    do {
        pcm.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
        // TODO : need to check pcm audio can be played ok or not
        // removing getParam part due to lm15u restriction.
        // assert occurs with getParam for pcm mode at audio renderer
        // but RTK might be need it
        pcm.nVersion.nVersion = OMX_VERSION;
        pcm.nPortIndex = data.nPortIndex;
        pcm.nSamplingRate = data.nSamplingRate;
        pcm.nChannels = data.nChannels;
        pcm.nBitPerSample = data.nBitPerSample;
        pcm.eEndian = data.eEndian;
        pcm.eNumData = data.eNumData;
        pcm.bInterleaved = data.bInterleaved;
        pcm.ePCMMode = data.ePCMMode;
        pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
        pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
        //OMX_AUDIO_CHANNELTYPE *a ={OMX_AUDIO_ChannelLF, OMX_AUDIO_ChannelRF};
        //memcpy(pcm.eChannelMapping, data.eChannelMapping,sizeof(data.eChannelMapping));

        BREAK_IF_NONZERO(setParam(OMX_IndexParamAudioPcm, &pcm), "set audio pcm parameter >> %d", pcm.nPortIndex);
        ret = NDL_ESP_RESULT_SUCCESS;
    } while(0);
    return ret;
}

int Component::connectOutputComponent(Component* output)
{
    //FIXME : check buffer size.
    //Audio render return error when decoder output buffer size is different from render's input buffer size.
    int output_component_buffer_count = output->getInputBufferCount();
    int output_component_buffer_size = output->getInputBufferSize();
    int buf_count = output_component_buffer_count > getOutputBufferCount() ?
        output_component_buffer_count:getOutputBufferCount();
    int buf_size = output_component_buffer_size > getOutputBufferSize() ?
        output_component_buffer_size:getOutputBufferSize();
    int ret = NDL_ESP_RESULT_FAIL;
    do {
        NDLLOG(LOGTAG, NDL_LOGD, "buf set: %d/%d/%d %d/%d/%d\n",
                buf_count, output_component_buffer_count,
                output_buffer_count_, buf_size,
                output_component_buffer_size, output_buffer_size_);

        BREAK_IF_NONZERO(output->configureInputBuffers(buf_count, buf_size), "reconfigure input buffer");
        BREAK_IF_NONZERO(configureOutputBuffers(buf_count, buf_size), "reconfigure output buffer");

        BREAK_IF_NONZERO(allocateOutputBuffer(), "allocating output buffers");
        BREAK_IF_NONZERO(output->useBuffer(output->getInputPortIndex(), this, getOutputPortIndex()), "set use buffer");
        ret = NDL_ESP_RESULT_SUCCESS;
    } while(0);

    return ret;
}


//Not support in RPI
void Component::getVideoInfo(NDL_ESP_VIDEO_INFO_T* videoInfo, void* data)
{
}

void Component::printComponentInfo() const
{
    NDLLOG(LOGTAG, NDL_LOGI, "<%s>", getComponentName());
    NDLLOG(LOGTAG, NDL_LOGI, "inputPort: %d, outputPort: %d, clockPort: %d",
            getInputPortIndex(), getOutputPortIndex(), getClockInputPortIndex());
    NDLLOG(LOGTAG, NDL_LOGI, "InputBufferCount : %d, InputBufferSize: %d, OutputBufferCount: %d, OutputBufferSize: %d",
            getInputBufferCount(), getInputBufferSize(), getOutputBufferCount(), getOutputBufferSize());
}

