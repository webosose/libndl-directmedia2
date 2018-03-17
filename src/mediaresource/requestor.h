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

#ifndef MEDIA_RESOURCE_REQUESTOR_H_
#define MEDIA_RESOURCE_REQUESTOR_H_

#include <string>
#include <functional>
#include <memory>
#include <map>
#include <dto_types.h>
#include "ndl-directmedia2/media-common.h"

namespace mrc {
    class ResourceCalculator;
}

namespace uMediaServer {
    class ResourceManagerClient;
}

class MDCClient;

class MDCContentProvider;

namespace NDL_Esplayer {

    struct videoResData_t {
        NDL_ESP_VIDEO_CODEC vcodec;
        int width;
        int height;
        int frameRate;
        NDL_ESP_SCAN_TYPE escanType;
        NDL_ESP_3D_TYPE e3DType;
    };

    struct audioResData_t {
        NDL_ESP_AUDIO_CODEC acodec;
        int version;
        int channel;
    };


    typedef std::function<void()> Functor;
    typedef std::function<bool(int32_t)> PlaneIDFunctor;
    typedef mrc::ResourceCalculator MRC;
    typedef std::multimap<std::string, int> PortResource_t;

    class ResourceRequestor
    {
        public:
            ResourceRequestor();
            explicit ResourceRequestor(const std::string& appId);
            virtual ~ResourceRequestor();

            const std::string getConnectionId() const { return connectionId_; };
            void registerUMSPolicyActionCallback(Functor callback) { cb_ = callback; };
            void registerPlaneIdCallback(PlaneIDFunctor callback) { planeIdCb_ = callback; };

            void unregisterWithMDC();

            bool acquireResources(NDL_ESP_META_DATA* meta, PortResource_t& resourceMMap);

            bool releaseResource();

            bool enableScreenSaver();
            bool endOfStream();
            bool disableScreenSaver();

            bool notifyForeground() const;
            bool notifyBackground() const;
            bool notifyActivity() const;

            void allowPolicyAction(const bool allow);

            int setVideoDisplayWindow(const long left, const long top,
                    const long width, const long height,
                    const bool isFullScreen) const;

            int setVideoCustomDisplayWindow(const long src_left, const long src_top,
                    const long src_width, const long src_height,
                    const long dst_left, const long dst_top,
                    const long dst_width, const long dst_height,
                    const bool isFullScreen) const;

            int muteAudio(bool mute){ return 0; }
            int muteVideo(bool mute){ return 0; }

            bool mediaContentReady(bool state);

            bool setVideoInfo(const NDL_ESP_VIDEO_INFO_T videoInfo);
            bool setVideoInfo(const videoResData_t videoResData);

        private:
            void ResourceRequestorInit();
            bool policyActionHandler(const char *action,
                    const char *resources,
                    const char *requestorType,
                    const char *requestorName,
                    const char *connectionId);
            void planeIdHandler(int32_t planePortIdx);

            bool parsePortInformation(const std::string& payload, PortResource_t& resourceMMap);
            bool parseResources(const std::string& payload, std::string& resources);

            // translate enum type from omx player to resource calculator
            int translateVideoCodec(const NDL_ESP_VIDEO_CODEC vcodec) const;
            int translateAudioCodec(const NDL_ESP_AUDIO_CODEC acodec) const;
            int translateScanType(const NDL_ESP_SCAN_TYPE escanType) const;
            int translate3DType(const NDL_ESP_3D_TYPE e3DType) const;

            std::shared_ptr<MRC> rc_;
            std::shared_ptr<uMediaServer::ResourceManagerClient> umsRMC_;
            std::shared_ptr<MDCClient> umsMDC_;
            std::shared_ptr<MDCContentProvider> umsMDCCR_;
            std::string appId_;
            std::string connectionId_;
            Functor cb_;
            PlaneIDFunctor planeIdCb_;
            std::string acquiredResource_;
            uMediaServer::mdc::video_info_t video_info_;
            bool allowPolicy_;
    };

} //namespace NDL_Esplayer

#endif 	// MEDIA_RESOURCE_REQUESTOR_H_
