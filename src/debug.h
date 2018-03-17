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

#ifndef NDL_DIRECTMEDIA2_DEBUG_H_
#define NDL_DIRECTMEDIA2_DEBUG_H_

#include <assert.h>
#include <sys/time.h>

#define GETTIME(a, b) gettimeofday(a,b);

/** Compute difference between start and end */
#define TIME_DIFF(start, end, diff) \
    diff = ((end.tv_sec - start.tv_sec) * 1000000) + \
(end.tv_usec - start.tv_usec);

//#define FILEDUMP
#ifdef FILEDUMP
char mInFile[256];
char mOutFile[256];
struct timeval mTime;

#define DUMP_PATH "/tmp/"
#define INPUT_DUMP_EXT "aac"
#define OUTPUT_DUMP_EXT "pcm"


#define GENERATE_FILE_NAMES() {                         \
    GETTIME(&mTime, NULL);                         \
    strcpy(mInFile, "");                                \
    sprintf(mInFile, "%s_%ld.%ld.%s", DUMP_PATH,  \
            mTime.tv_sec, mTime.tv_usec,      \
            INPUT_DUMP_EXT);                            \
    strcpy(mOutFile, "");                               \
    sprintf(mOutFile, "%s_%ld.%ld.%s", DUMP_PATH,\
            mTime.tv_sec, mTime.tv_usec,      \
            OUTPUT_DUMP_EXT);                           \
}

#define CREATE_DUMP_FILE(m_filename) {                  \
    FILE *fp = fopen(m_filename, "wb");                 \
    if (fp != NULL) {                                   \
        NDLLOG(LOGTAG, NDL_LOGE, "Opened file %s", m_filename);            \
        fclose(fp);                                     \
    } else {                                            \
        NDLLOG(LOGTAG, NDL_LOGE," Could not open file %s(%s)", m_filename, strerror(errno));    \
    }                                                   \
}

#define DUMP_TO_FILE(m_filename, m_buf, m_size)         \
{                                                       \
    FILE *fp = fopen(m_filename, "ab");                 \
    if (fp != NULL && m_buf != NULL) {                  \
        int i;                                          \
        i = fwrite(m_buf, 1, m_size, fp);               \
        NDLLOG(LOGTAG, NDL_LOGE,"fwrite ret %d to write %d", i, m_size);  \
        if (i != (int)m_size) {                         \
            NDLLOG(LOGTAG, NDL_LOGE,"Error in fwrite, returned %d", i);   \
            perror("Error in write to file");           \
        }                                               \
        fclose(fp);                                     \
    } else {                                            \
        NDLLOG(LOGTAG, NDL_LOGE,"Could not write to file %s(%s)", m_filename, strerror(errno));\
        if (fp != NULL)                                 \
        fclose(fp);                                 \
    }                                                   \
}
#endif


namespace NDL_Esplayer {

    //TODO: remove or substitute verbose level if pmlog is used.
    enum { NDL_LOGV=0, NDL_LOGD, NDL_LOGI, NDL_LOGE };


#ifndef USE_STDOUT_LOG
#define USE_STDOUT_LOG  0 // 0 :pmlog, 1 :standdard output log
#endif

#if USE_STDOUT_LOG
#define USE_PMLOG     0
#define LOG_TIME      1
#define LOG_LEVEL     1 // 0 :VERBOS ] 1:DEBUG ] 2:INFO ] 3:ERROR
#else
#define USE_PMLOG     1
#define LOG_TIME      0
#endif

#define LOG_TID         0

    void printLog(const char* TAG, int level, const char* fmt, ...);


#if USE_STDOUT_LOG
#define NDLLOG( TAG, LEVEL, ...)\
    do {\
        if ( LEVEL >= LOG_LEVEL )\
        printLog( TAG, LEVEL, __VA_ARGS__);\
    } while(0)
#else
#define NDLLOG( TAG, LEVEL, ...)\
    do {\
        printLog( TAG, LEVEL, __VA_ARGS__);\
    } while(0)
#endif



#ifndef LOGTAG
#define LOGTAG  "debug" // to use different tag, define LOGTAG before including .
#endif

    // for SDET automation test
#define SDETTAG "sdet "


#define LOG_IF_NONZERO(cond, format, args...) {   \
    if(int ret = cond) {                        \
        NDLLOG(LOGTAG, NDL_LOGE,                  \
                format ", error(%s:%d) : %s => 0x%x", ##args,\
                __func__, __LINE__, #cond, ret);    \
    } else {                                    \
        NDLLOG(LOGTAG, NDL_LOGD, format, ##args); \
    }                                           \
}

#define LOG_AND_RETURN_IF_NONZERO(cond, format, args...) {   \
    if(int ret = cond) {                        \
        NDLLOG(LOGTAG, NDL_LOGE,                  \
                format ", error(%s:%d) : %s => 0x%x", ##args,\
                __func__, __LINE__, #cond, ret);    \
        return NDL_ESP_RESULT_FAIL;               \
    } else {                                    \
        NDLLOG(LOGTAG, NDL_LOGD, format, ##args); \
    }                                           \
}

#define BREAK_IF_NONZERO(cond, format, args...) { \
    if(int ret = cond) {                        \
        NDLLOG(LOGTAG, NDL_LOGE,                  \
                format ", error(%s:%d) : %s => 0x%x", ##args,\
                __func__, __LINE__, #cond, ret);    \
        break;                                    \
    } else {                                    \
        NDLLOG(LOGTAG, NDL_LOGD, format, ##args); \
    }                                           \
}

#define RETURN_IF_NONZERO(cond, format, args...) { \
    if(int ret = cond) {                        \
        NDLLOG(LOGTAG, NDL_LOGE,                  \
                format ", error(%s:%d) : %s => 0x%x", ##args,\
                __func__, __LINE__, #cond, ret);    \
        return NDL_ESP_RESULT_FAIL;                                    \
    } else {                                    \
        NDLLOG(LOGTAG, NDL_LOGD, format, ##args); \
    }                                           \
}

#define RETURN_FAIL_IF_TRUE(cond, format, args...) { \
    if (cond) { \
        NDLLOG(LOGTAG, NDL_LOGE, format \
                ", error(%s:%d) : %s => %d", ##args, __func__, __LINE__, #cond, cond); \
        return NDL_ESP_RESULT_FAIL; \
    } \
}
#define RETURN_FAIL_IF_NONZERO RETURN_FAIL_IF_TRUE

#define RETURN_SUCCESS_IF_NONTRUE(cond, format, args...) { \
    if (cond) { \
        NDLLOG(LOGTAG, NDL_LOGE, format \
                ", error(%s:%d) : %s => %d", ##args, __func__, __LINE__, #cond, cond); \
        return NDL_ESP_RESULT_FAIL; \
    } else { \
        return NDL_ESP_RESULT_SUCCESS; \
    } \
}

#define NDLASSERT( cond ) {\
    if(!(cond)) {      \
        NDLLOG(LOGTAG, NDL_LOGE, "ASSERT FAILED : %s:%d:%s: %s", \
                __FILE__, __LINE__, __func__, #cond);\
        assert(0);\
    }\
}

    } // namespace NDL_Esplayer

#endif // #ifndef NDL_DIRECTMEDIA2_DEBUG_H_
