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

#include "ndl-directmedia2/esplayer-api.h"
#include "esplayer.h"

#define LOGTAG "ESapi "
#include "debug.h"
using namespace NDL_Esplayer;


struct EsplayerWrapper
{
    EsplayerWrapper(const char* appid, NDL_EsplayerCallback callback, void* userdata) {
        esplayer = new Esplayer(appid, callback, userdata);
    };
    ~EsplayerWrapper() {
        delete esplayer;
    };

    Esplayer* esplayer;
};



NDL_EsplayerHandle NDL_EsplayerCreate(const char* appid,
        NDL_EsplayerCallback callback,
        void* userdata)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerCreate!");
    EsplayerWrapper* espWrapper = new EsplayerWrapper(appid, callback, userdata);
    return (NDL_EsplayerHandle)espWrapper;
}


void NDL_EsplayerDestroy(NDL_EsplayerHandle player)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerDestroy!");

    NDLASSERT(player);
    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    delete espWrapper;
}


int NDL_EsplayerGetConnectionId(NDL_EsplayerHandle player, char* buf, size_t buf_len)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerGetConnectionId!");

    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->getConnectionId(buf, buf_len);
}


int NDL_EsplayerLoad(NDL_EsplayerHandle player,
        NDL_ESP_META_DATA* meta)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerLoad!");

    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->load(meta);
}


int NDL_EsplayerLoadEx(NDL_EsplayerHandle player,
        NDL_ESP_META_DATA* meta, NDL_ESP_PTS_UNITS units)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerLoadEx! %d", units);

    NDLASSERT(player);
    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->loadEx(meta, units);
}


int NDL_EsplayerUnload(NDL_EsplayerHandle player)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerUnload!");

    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->unload();
}


int NDL_EsplayerPlay(NDL_EsplayerHandle player)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerPlay!");

    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->play();
}


int NDL_EsplayerPause(NDL_EsplayerHandle player)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerPause!");
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->pause();
}


int NDL_EsplayerFeedData(NDL_EsplayerHandle player,
        NDL_EsplayerBuffer buff)
{
    NDLASSERT(player && buff);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    NDLLOG(LOGTAG, NDL_LOGV, "use_count:%d before feeding", buff.use_count());
    int ret = (espWrapper->esplayer)->feedData(buff);
    NDLLOG(LOGTAG, NDL_LOGV, "use_count:%d after feeding", buff.use_count());
    return ret;
}


int NDL_EsplayerStepFrame(NDL_EsplayerHandle player)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->stepFrame();
}


int NDL_EsplayerFlush(NDL_EsplayerHandle player)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->flush();
}


int NDL_EsplayerGetBufferLevel(
        NDL_EsplayerHandle player,
        NDL_ESP_STREAM_T type,
        uint32_t* level)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->getBufferLevel(type, level);
}


NDL_ESP_STATUS NDL_EsplayerGetStatus(NDL_EsplayerHandle player)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_STATUS_IDLE;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->getStatus();
}


int NDL_EsplayerGetMediatime(
        NDL_EsplayerHandle player,
        int64_t* start_time,
        int64_t* current_time)
{
    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerGetMediatime!");


    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->getMediaTime(start_time, current_time);
}


int NDL_EsplayerSetPlaybackRate(
        NDL_EsplayerHandle player,
        int rate)
{

    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerSetPlaybackRate %d!",rate);

    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->setPlaybackRate(rate);
}


int NDL_EsplayerReloadAudio(NDL_EsplayerHandle player,
        NDL_ESP_META_DATA* meta)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->reloadAudio(meta);
}


int NDL_EsplayerSet3DType(NDL_EsplayerHandle player, NDL_ESP_3D_TYPE e3DType)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->set3DType(e3DType);
}


int NDL_EsplayerSetTrickMode(NDL_EsplayerHandle player, int enable)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;

    return (espWrapper->esplayer)->setTrickMode(enable ? true : false);
}

int NDL_EsplayerSetVolume(NDL_EsplayerHandle player, int volume, int duration, NDL_ESP_EASE_TYPE type)
{
    NDLASSERT(player);
    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;

    return (espWrapper->esplayer)->setVolume(volume, duration, type);
}

///////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// Display //////////////////////////////////////////


int NDL_EsplayerSetVideoDisplayWindow(NDL_EsplayerHandle player,
        long left, long top, long width, long height, int isFullScreen)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->setVideoDisplayWindow(left, top, width, height,
            (bool)isFullScreen);
}


int NDL_EsplayerSetVideoCustomDisplayWindow(NDL_EsplayerHandle player,
        long src_left, long src_top, long src_width, long src_height,
        long dst_left, long dst_top, long dst_width, long dst_height,
        int isFullScreen)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->setVideoCustomDisplayWindow(src_left, src_top, src_width, src_height,
            dst_left, dst_top, dst_width, dst_height,
            (bool)isFullScreen);
}


int NDL_EsplayerSetAppForegroundState(NDL_EsplayerHandle player, NDL_ESP_APP_STATE appState)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->notifyForegroundState(appState);
}


int NDL_EsplayerMuteAudio(NDL_EsplayerHandle player, int mute)
{

    NDLLOG(LOGTAG, NDL_LOGI, "NDL_EsplayerMuteAudio %d!",mute);


    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->muteAudio((bool)mute);
}


int NDL_EsplayerMuteVideo(NDL_EsplayerHandle player, int mute)
{
    NDLASSERT(player);
    if (!player)
        return NDL_ESP_RESULT_FAIL;

    EsplayerWrapper* espWrapper = (EsplayerWrapper*)player;
    return (espWrapper->esplayer)->muteVideo((bool)mute);
}
