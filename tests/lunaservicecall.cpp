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

#include "lunaservicecall.h"

namespace {
    typedef struct LSErrorWrapper : LSError {
        LSErrorWrapper() { LSErrorInit(this); }
        ~LSErrorWrapper() { LSErrorFree(this); }
    } ls_error_t;
}


LunaServiceCall::LunaServiceCall() {
    ls_error_t error;
    if (!LSRegister("com.webos.esplayer-test", &handle_, &error)) {
        if (LSErrorIsSet(&error))
            LSErrorPrint(&error, stderr);
    } else {
        printf("registered to luna service\n");
    }

    context_ = g_main_context_new();
    loop_ = g_main_loop_new(context_, false);
    thread_ = std::thread(g_main_loop_run, loop_);
    LSGmainAttach(handle_, loop_, &error);
}

LunaServiceCall::~LunaServiceCall() {
    if (handle_) {
        ls_error_t error;
        if (!LSUnregister(handle_, &error)) {
            if (LSErrorIsSet(&error))
                LSErrorPrint(&error, stderr);
        } else {
            printf("unregistered to luna service\n");
        }
    }
    g_main_loop_quit(loop_);
    g_main_loop_unref(loop_);
    g_main_context_unref(context_);
    thread_.join();
}

bool LunaServiceCall::call(const char* uri, const char* payload) {
    if (!handle_)
        return false;
    ls_error_t error;
    printf("LSCall: uri:%s, payload:%s\n", uri, payload);
    bool ret = LSCall(handle_, uri, payload,
            [] (LSHandle * sh, LSMessage * reply, void * ctx) {
            std::string r = LSMessageGetPayload(reply);
            printf("LSCall callback : %s\n", r.c_str());
            return true;
            },
            NULL, NULL, &error);
    if(!ret) {
        if (LSErrorIsSet(&error))
        {
            LSErrorPrint(&error, stderr);
        }
        return false;
    }
    printf("LSCall succeed\n");
    return true;
}

bool LunaServiceCall::enableSubtitle(const char* pipelineId) {
    const char* uri = "luna://com.webos.service.tv.subtitle/enableSubtitle";
    char payload[64];
    sprintf(payload, "{\"pipelineId\":\"%s\"}", pipelineId);
    return call(uri, payload);
}
