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
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <PmLogLib.h>

#include "debug.h"

using namespace NDL_Esplayer;


#define MAX_LOG_LENGTH  2000

namespace {
#if USE_PMLOG
#define NDL_DIRECTMEDIA2_LOG_CONTEXT  "ndl.directmedia2"
    static PmLogContext pmlog_context = NULL;

    static inline void printLogImpl(int level, const char* msg)
    {
        if (pmlog_context == NULL) {
            PmLogErr err = PmLogGetContext(NDL_DIRECTMEDIA2_LOG_CONTEXT, &pmlog_context);
            NDLASSERT(err == kPmLogErr_None);
        }

        PmLogLevel pmlevel = kPmLogLevel_Error;

        switch(level) {
            case NDL_LOGV: return; //TODO: pmlog doesn't have verbose level
            case NDL_LOGD: pmlevel = kPmLogLevel_Debug; break;
            case NDL_LOGI: pmlevel = kPmLogLevel_Info; break;
            case NDL_LOGE: pmlevel = kPmLogLevel_Error; break;
            default:
                           PmLogString(pmlog_context, kPmLogLevel_Error, "ERROR", NULL, "check log level!!!");
                           break;
        }

        if (pmlevel == kPmLogLevel_Debug) {
            PmLogString(pmlog_context, pmlevel, NULL/*must null*/, NULL/*must null*/, msg);
        } else {
            PmLogString(pmlog_context, pmlevel, "MSG"/*msgid*/, NULL/*kvpairs*/, msg);
        }
    }
#else
#define log_printf( ... )  printf(__VA_ARGS__)
    static inline void printLogImpl(int level, const char* msg)
    {
        char level_c= ' ';
        switch(level) {
            case NDL_LOGV: level_c = 'V'; break;
            case NDL_LOGD: level_c = 'D'; break;
            case NDL_LOGI: level_c = 'I'; break;
            case NDL_LOGE: level_c = 'E'; break;
        }
        log_printf("  [%c] %s\n", level_c, msg);
    }
#endif
} // namespace {

namespace NDL_Esplayer {

    void printLog(const char* tag, int level, const char* fmt, ...)
    {
        char msg[MAX_LOG_LENGTH] = {0};
        char* msgend = msg + MAX_LOG_LENGTH;
        char* msgpos = msg;

#if LOG_TIME
        struct timeval now;
        gettimeofday( &now, NULL );
        msgpos += strftime( msgpos, msgend - msgpos,"%H:%M:%S", localtime(&now.tv_sec));
        msgpos += snprintf(msgpos, msgend - msgpos, ".%06ld ", now.tv_usec);
#endif

#if LOG_TID
        msgpos += snprintf(msgpos, msgend - msgpos, "[%lx] ", pthread_self());
#endif

        msgpos += snprintf(msgpos, msgend - msgpos, "[%s] ", tag);


        va_list arg_ptr;
        va_start(arg_ptr, fmt);
        vsnprintf(msgpos, msgend - msgpos, fmt, arg_ptr);
        va_end(arg_ptr);

        printLogImpl(level, msg);
    }

} // namespace NDL_Esplayer {
