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

#include <string>
#include <set>
#include <pbnjson.hpp>
#include "requestor.h"
#include <resource_calculator.h>
#include <ResourceManagerClient.h>
#include <MDCClient.h>
#include <MDCContentProvider.h>

#define LOGTAG "ResourceRequestor"
#include "debug.h"

using namespace NDL_Esplayer;
using namespace std;
using mrc::ResourceCalculator;
using namespace pbnjson;

#define FAKE_WIDTH_MAX 0
#define FAKE_HEIGHT_MAX 0
#define FAKE_FRAMERATE_MAX 0

ResourceRequestor::ResourceRequestor()
    : rc_(shared_ptr<MRC>(MRC::create())),
    cb_(nullptr),
    allowPolicy_(true)
{
    ResourceRequestorInit();
}

ResourceRequestor::ResourceRequestor(const std::string& appId)
    : rc_(shared_ptr<MRC>(MRC::create())),
    cb_(nullptr),
    allowPolicy_(true)
{
    this->appId_ = appId;
    ResourceRequestorInit();
}

void ResourceRequestor::ResourceRequestorInit()
{
    umsRMC_ = make_shared<uMediaServer::ResourceManagerClient> ();
    umsMDC_ = make_shared<MDCClient> ();

    umsRMC_->registerPipeline("media");           // only rmc case
    connectionId_ = umsRMC_->getConnectionID();   // after registerPipeline

    umsMDC_->registerMedia(connectionId_, appId_);

    umsMDCCR_ = make_shared<MDCContentProvider> (connectionId_);

    if ("" == connectionId_ ) {
        NDLASSERT(0);
    }

    if (nullptr != umsMDCCR_) {
        bool res = umsMDCCR_->registerPlaneIdCallback(std::bind(&ResourceRequestor::planeIdHandler,
                    this,
                    std::placeholders::_1));
        NDLLOG(LOGTAG, NDL_LOGI, "PlaneID callback register : %s", res ? "success!" : "fail!");
    }

    umsRMC_->registerPolicyActionHandler(
            std::bind(&ResourceRequestor::policyActionHandler,
                this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3,
                std::placeholders::_4,
                std::placeholders::_5));
}

void ResourceRequestor::unregisterWithMDC()
{
    if (!umsMDC_->unregisterMedia())
        NDLLOG(LOGTAG, NDL_LOGE, "MDC unregister error");
}

bool ResourceRequestor::endOfStream()
{
    return umsMDC_->updatePlaybackState(PLAYBACK_STOPPED);
}

bool ResourceRequestor::enableScreenSaver()
{
    return umsMDC_->updatePlaybackState(PLAYBACK_PAUSED);
}


bool ResourceRequestor::disableScreenSaver()
{
    return umsMDC_->updatePlaybackState(PLAYBACK_PLAYING);
}


ResourceRequestor::~ResourceRequestor()
{
    if (!acquiredResource_.empty()) {
        umsRMC_->release(acquiredResource_);
        acquiredResource_ = "";
    }
}

bool ResourceRequestor::acquireResources(NDL_ESP_META_DATA* meta, PortResource_t& resourceMMap)
{
    videoResData_t videoResData = { meta->video_codec, FAKE_WIDTH_MAX, FAKE_HEIGHT_MAX, FAKE_FRAMERATE_MAX,
        SCANTYPE_PROGRESSIVE, E3DTYPE_NONE };
    audioResData_t audioResData = { meta->audio_codec, 0, 0 };

    // ResourceCaculator & ResourceManager is changed in WebOS 3.0
    mrc::ResourceList AResource;
    mrc::ResourceList audioOptions;
    mrc::ResourceListOptions VResource;
    mrc::ResourceListOptions finalOptions;


    AResource = rc_->calcAdecResources( (MRC::AudioCodecs)translateAudioCodec(audioResData.acodec),
            audioResData.version,
            audioResData.channel );
    mrc::concatResourceList(&audioOptions, &AResource);

    VResource = rc_->calcVdecResourceOptions( (MRC::VideoCodecs)translateVideoCodec(videoResData.vcodec),
            videoResData.width,
            videoResData.height,
            videoResData.frameRate,
            (MRC::ScanType)translateScanType(videoResData.escanType),
            (MRC::_3DType)translate3DType(videoResData.e3DType) );

    finalOptions.push_back(audioOptions);
    mrc::concatResourceListOptions(&finalOptions, &VResource);

    JSchemaFragment input_schema("{}");
    JGenerator serializer(NULL);
    string payload;
    string response;

    JValue objArray = pbnjson::Array();
    for(auto option : finalOptions) {
        for (auto it : option) {
            JValue obj = pbnjson::Object();
            obj.put("resource", it.type);
            obj.put("qty", it.quantity);
            NDLLOG(LOGTAG, NDL_LOGI, "calculator return : %s, %d", it.type.c_str(), it.quantity);
            objArray << obj;
        }
    }

    if (!serializer.toString(objArray, input_schema, payload)) {
        NDLLOG(LOGTAG, NDL_LOGE, "[%s], fail to serializer to string", __func__);
        return false;
    }

    NDLLOG(LOGTAG, NDL_LOGI, "send acquire to uMediaServer");

    if (!umsRMC_->acquire(payload, response)) {
        NDLLOG(LOGTAG, NDL_LOGE, "fail to acquire!!! response : %s", response.c_str());
        return false;
    }

    try {
        parsePortInformation(response, resourceMMap);
        parseResources(response, acquiredResource_);
    } catch (const std::runtime_error & err) {
        NDLLOG(LOGTAG, NDL_LOGE, "[%s:%d] err=%s, response:%s",
                __func__, __LINE__, err.what(), response.c_str());
        return false;
    }

    setVideoInfo(videoResData);

    NDLLOG(LOGTAG, NDL_LOGI, "acquired Resource : %s", acquiredResource_.c_str());
    return true;
}

bool ResourceRequestor::releaseResource()
{
    if (acquiredResource_.empty()) {
        NDLLOG(LOGTAG, NDL_LOGI, "[%s], resource already empty", __func__);
        return true;
    }

    NDLLOG(LOGTAG, NDL_LOGI, "send release to uMediaServer. resource : %s", acquiredResource_.c_str());

    if (!umsRMC_->release(acquiredResource_)) {
        NDLLOG(LOGTAG, NDL_LOGE, "release error : %s", acquiredResource_.c_str());
        return false;
    }

    acquiredResource_ = "";
    return true;
}

bool ResourceRequestor::notifyForeground() const
{
    umsRMC_->notifyForeground();
    return true;
}

bool ResourceRequestor::notifyBackground() const
{
    umsRMC_->notifyBackground();
    return true;
}

bool ResourceRequestor::notifyActivity() const
{
    umsRMC_->notifyActivity();
    return true;
}

void ResourceRequestor::allowPolicyAction(const bool allow)
{
    allowPolicy_ = allow;
}

bool ResourceRequestor::policyActionHandler(const char *action,
        const char *resources,
        const char *requestorType,
        const char *requestorName,
        const char *connectionId)
{
    if (allowPolicy_) {
        if (nullptr != cb_) {
            cb_();
        }
        if (!umsRMC_->release(acquiredResource_)) {
            NDLLOG(LOGTAG, NDL_LOGE, "release error in policyActionHandler: %s", acquiredResource_.c_str());
            return false;
        }
    }

    return allowPolicy_;
}

bool ResourceRequestor::parsePortInformation(const std::string& payload, PortResource_t& resourceMMap)
{
    JDomParser parser;
    JSchemaFragment input_schema("{}");
    if (!parser.parse(payload, input_schema)) {
        throw std::runtime_error("payload parsing failure during parsePortInformation");
    }

    JValue parsed = parser.getDom();
    if (!parsed.hasKey("resources")) {
        throw std::runtime_error("payload must have \"resources key\"");
    }

    for (int i=0; i<parsed["resources"].arraySize(); ++i) {
        resourceMMap.insert( std::make_pair( parsed["resources"][i]["resource"].asString(), parsed["resources"][i]["index"].asNumber<int32_t>()) );
    }

    for (auto& it : resourceMMap) {
        NDLLOG(LOGTAG, NDL_LOGD, "port Resource - %s, : [%d] ", it.first.c_str(), it.second);
    }

    return true;
}

bool ResourceRequestor::parseResources(const std::string& payload, std::string& resources)
{
    JDomParser parser;
    JSchemaFragment input_schema("{}");
    JGenerator serializer(NULL);

    if (!parser.parse(payload, input_schema)) {
        throw std::runtime_error("payload parsing failure during parseResources");
    }

    JValue parsed = parser.getDom();
    if (!parsed.hasKey("resources")) {
        throw std::runtime_error("payload must have \"resources key\"");
    }

    JValue objArray = pbnjson::Array();
    for (int i=0; i<parsed["resources"].arraySize(); ++i) {
        JValue obj = pbnjson::Object();
        obj.put("resource", parsed["resources"][i]["resource"].asString());
        obj.put("index", parsed["resources"][i]["index"].asNumber<int32_t>());
        objArray << obj;
    }

    if (!serializer.toString(objArray, input_schema, resources)) {
        throw std::runtime_error("fail to serializer toString during parseResources");
    }

    return true;
}

int ResourceRequestor::translateVideoCodec(const NDL_ESP_VIDEO_CODEC vcodec) const
{
    MRC::VideoCodec ev = MRC::kVideoEtc;

    switch (vcodec) {
        case NDL_ESP_VIDEO_NONE:
            ev = MRC::kVideoEtc;    break;
        case NDL_ESP_VIDEO_CODEC_H264:
            ev = MRC::kVideoH264;   break;
        case NDL_ESP_VIDEO_CODEC_H265:
            ev = MRC::kVideoH265;   break;
        case NDL_ESP_VIDEO_CODEC_MPEG:
            ev = MRC::kVideoMPEG;   break;
        case NDL_ESP_VIDEO_CODEC_MVC:
            ev = MRC::kVideoMVC;    break;
        case NDL_ESP_VIDEO_CODEC_SVC:
            ev = MRC::kVideoSVC;    break;
        case NDL_ESP_VIDEO_CODEC_VP8:
            ev = MRC::kVideoVP8;    break;
        case NDL_ESP_VIDEO_CODEC_VP9:
            ev = MRC::kVideoVP9;    break;
        case NDL_ESP_VIDEO_CODEC_RM:
            ev = MRC::kVideoRM;     break;
        case NDL_ESP_VIDEO_CODEC_AVS:
            ev = MRC::kVideoAVS;    break;
        case NDL_ESP_VIDEO_CODEC_MJPEG:
            ev = MRC::kVideoMJPEG;  break;
        default:
            ev = MRC::kVideoEtc;
            break;
    }

    return (int)ev;
}

int ResourceRequestor::translateAudioCodec(const NDL_ESP_AUDIO_CODEC acodec) const
{
    MRC::AudioCodec ea = MRC::kAudioEtc;

    switch (acodec) {
        // currently webOS TV only considers "audio/mpeg" or not
        case NDL_ESP_AUDIO_CODEC_MP2:
        case NDL_ESP_AUDIO_CODEC_MP3:
            ea = MRC::kAudioMPEG;
            break;
        case NDL_ESP_AUDIO_NONE:
            ea = MRC::kAudioEtc;
            break;
        default:
            ea = MRC::kAudioEtc;
            break;
    }

    return (int)ea;
}

int ResourceRequestor::translateScanType(const NDL_ESP_SCAN_TYPE escanType) const
{
    MRC::ScanType scan = MRC::kScanProgressive;

    switch (escanType) {
        case SCANTYPE_PROGRESSIVE:
            scan = MRC::kScanProgressive;
            break;
        case SCANTYPE_INTERLACED:
            scan = MRC::kScanInterlaced;
            break;
        default:
            break;
    }

    return (int)scan;
}

int ResourceRequestor::translate3DType(const NDL_ESP_3D_TYPE e3DType) const
{
    MRC::_3DType my3d = MRC::k3DNone;

    switch (e3DType) {
        case E3DTYPE_NONE:
            my3d = MRC::k3DNone;
            break;
            //TODO: resource calculator defines below 2 types. but not used.
            /*
               case E3DTYPE_SEQUENTIAL:
               my3d = MRC::k3DSequential;
               break;
               case E3DTYPE_MULTISTREAM:
               my3d = MRC::k3DMultiStream;
               break;
               */
        default:
            my3d = MRC::k3DNone;
            break;
    }

    return (int)my3d;
}

int ResourceRequestor::setVideoDisplayWindow(const long left, const long top,
        const long width, const long height,
        const bool isFullScreen) const
{
    if (isFullScreen)
        return umsMDC_->switchToFullscreen() ? 0 :1;

    return umsMDC_->setDisplayWindow(window_t(left, top, width, height)) ? 0 :1;
}
int ResourceRequestor::setVideoCustomDisplayWindow(const long src_left, const long src_top,
        const long src_width, const long src_height,
        const long dst_left, const long dst_top,
        const long dst_width, const long dst_height,
        const bool isFullScreen) const
{
    if (isFullScreen)
        return umsMDC_->switchToFullscreen() ? 0 :1;

    return umsMDC_->setDisplayWindow(window_t(src_left, src_top, src_width, src_height),
            window_t(dst_left, dst_top, dst_width, dst_height)) ? 0 :1;
}

bool ResourceRequestor::mediaContentReady(bool state)
{
    return umsMDCCR_->mediaContentReady(state);
}

bool ResourceRequestor::setVideoInfo(const NDL_ESP_VIDEO_INFO_T videoInfo){
    video_info_.width = (int)videoInfo.width;
    video_info_.height = (int)videoInfo.height;
    video_info_.frame_rate = (float)videoInfo.framerateNum / (float)videoInfo.framerateDen;
    video_info_.scan_type = videoInfo.SCANTYPE == SCANTYPE_PROGRESSIVE ? "progressive" : "interlaced";
    video_info_.pixel_aspect_ratio.width = (int)videoInfo.PARwidth;
    video_info_.pixel_aspect_ratio.height = (int)videoInfo.PARheight;
    video_info_.path = "network";
    video_info_.adaptive = true;
    return umsMDCCR_->setVideoInfo(video_info_);
}

bool ResourceRequestor::setVideoInfo(const videoResData_t videoResData){
    video_info_.width =(int)videoResData.width;
    video_info_.height =(int)videoResData.height;
    video_info_.frame_rate = (float)videoResData.frameRate;
    video_info_.pixel_aspect_ratio.width =1;
    video_info_.pixel_aspect_ratio.height =1;
    video_info_.scan_type = videoResData.escanType == SCANTYPE_PROGRESSIVE ? "progressive" : "interlaced";
    video_info_.path = "network";
    video_info_.adaptive = true;
    return umsMDCCR_->setVideoInfo(video_info_);
}

void ResourceRequestor::planeIdHandler(int32_t planePortIdx)
{
    NDLLOG(LOGTAG, NDL_LOGI, "planePortIndex = %d", planePortIdx);
    if (nullptr != planeIdCb_) {
        bool res = planeIdCb_(planePortIdx);
        NDLLOG(LOGTAG, NDL_LOGI, "PlanePort[%d] register : %s",
                planePortIdx, res ? "success!" : "fail!");
    }
}
