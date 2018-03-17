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

#include"omxcore.h"
#include <bcm_host.h>
#include <OMXDrm.h>
#include <OMXLibAlsa.h>
#include<iostream>
using namespace NDL_Esplayer;

OmxCore *OmxCore::instance = nullptr;

OmxCore::OmxCore()
{
    bcm_host_init();
    OMX_Init();
}

OmxCore::~OmxCore()
{
    bcm_host_deinit();
    OMX_Deinit();
}

OMX_ERRORTYPE OmxCore::getHandle(OMX_HANDLETYPE* pHandle,
        OMX_STRING cComponentName,
        OMX_PTR pAppData,
        OMX_CALLBACKTYPE* pCallBacks)
{
    return OMX_GetHandle(pHandle, cComponentName, pAppData, pCallBacks);
}

OMX_ERRORTYPE OmxCore::getDRMHandle(OMX_HANDLETYPE* pHandle,
        OMX_STRING cComponentName,
        OMX_PTR pAppData,
        OMX_CALLBACKTYPE* pCallBacks)
{
    return OMXDRM_GetHandle(pHandle, cComponentName, pAppData, pCallBacks);
}

OMX_ERRORTYPE OmxCore::freeDRMHandle(OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE ret = OMXDRM_FreeHandle(hComponent);
    return ret;
}
OMX_ERRORTYPE OmxCore::getALSAHandle(OMX_HANDLETYPE* pHandle,
        OMX_STRING cComponentName,
        OMX_PTR pAppData,
        OMX_CALLBACKTYPE* pCallBacks)
{
    return OMXLIB_ALSA_GetHandle(pHandle, cComponentName, pAppData, pCallBacks);
}
OMX_ERRORTYPE OmxCore::freeALSAHandle(OMX_HANDLETYPE hComponent)
 {
     OMX_ERRORTYPE ret = OMXLIB_ALSA_FreeHandle(hComponent);
     return ret;
 }

OMX_ERRORTYPE OmxCore::freeHandle(OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE ret = OMX_FreeHandle(hComponent);
    return ret;
}

OMX_ERRORTYPE OmxCore::setupTunnel(OMX_HANDLETYPE hOutput,
        OMX_U32 nPortOutput,
        OMX_HANDLETYPE hInput,
        OMX_U32 nPortInput)
{
    return OMX_SetupTunnel(hOutput, nPortOutput, hInput, nPortInput);
}

OMX_ERRORTYPE OmxCore::getComponentsOfRole(OMX_STRING role,
        OMX_U32 *pNumComps,
        OMX_U8  **compNames)
{
    return OMX_GetComponentsOfRole(role, pNumComps, compNames);
}
