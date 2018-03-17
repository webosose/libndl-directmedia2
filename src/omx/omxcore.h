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

#ifndef NDL_DIRECTMEDIA2_OMX_CLIENTS_OMXCORE_H_
#define NDL_DIRECTMEDIA2_OMX_CLIENTS_OMXCORE_H_

#include <OMX_Core.h>
#include <stdlib.h>

namespace NDL_Esplayer {

    class OmxCore {
        public:
            static OmxCore& getInstance() {
                if (instance == nullptr) {
                    instance = new OmxCore();
                    atexit(destroyInstance);
                }
                return *instance;
            }
            static void destroyInstance() {
                if (instance != nullptr) {
                    delete instance;
                    instance = nullptr;
                }
            }

            OMX_ERRORTYPE getHandle(OMX_HANDLETYPE* pHandle,
                    OMX_STRING cComponentName,
                    OMX_PTR pAppData,
                    OMX_CALLBACKTYPE* pCallBacks);

            OMX_ERRORTYPE getDRMHandle(OMX_HANDLETYPE* pHandle,
                    OMX_STRING cComponentName,
                    OMX_PTR pAppData,
                    OMX_CALLBACKTYPE* pCallBacks);

            OMX_ERRORTYPE freeDRMHandle(OMX_HANDLETYPE hComponent);
            OMX_ERRORTYPE getALSAHandle( OMX_HANDLETYPE* pHandle,
                    OMX_STRING cComponentName,
                    OMX_PTR pAppData,
                    OMX_CALLBACKTYPE* pCallbacks);
            OMX_ERRORTYPE freeALSAHandle(OMX_HANDLETYPE hComponent);
            OMX_ERRORTYPE freeHandle(OMX_HANDLETYPE hComponent);

            OMX_ERRORTYPE setupTunnel(OMX_HANDLETYPE hOutput,
                    OMX_U32 nPortOutput,
                    OMX_HANDLETYPE hInput,
                    OMX_U32 nPortInput);

            OMX_ERRORTYPE getComponentsOfRole(OMX_STRING role,
                    OMX_U32 *pNumComps,
                    OMX_U8  **compNames);

        private:
            static OmxCore *instance;

        private:
            OmxCore();
            ~OmxCore();

            OmxCore(OmxCore const&) = delete;
            void operator=(OmxCore const&) = delete;
    };

} //namespace NDL_Esplayer

#endif //NDL_DIRECTMEDIA2_OMX_CLIENTS_OMXCORE_H_
