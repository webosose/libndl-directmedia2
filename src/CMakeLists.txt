# Copyright (c) 2008-2018 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0



# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 2.8.7)

message(STATUS "BUILDING ndl-directmedia2")

# find required packages
include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS_OTHER})

pkg_check_modules(LIBPBNJSON REQUIRED pbnjson_cpp)
webos_add_compiler_flags(ALL ${LIBPBNJSON_CFLAGS})

pkg_check_modules(PMLOGLIB REQUIRED PmLogLib)
webos_add_compiler_flags(ALL ${PMLOGLIB_CFLAGS})

pkg_check_modules(RESOURCE-CALCULATOR REQUIRED media-resource-calculator)
include_directories(${RESOURCE-CALCULATOR_INCLUDE_DIRS})

if(DEFINED RPI)
    message(STATUS "pkg_check_modules openmaxil for raspberrypi")
    pkg_check_modules(OMX REQUIRED openmaxil)

    message(STATUS "pkg_check_modules libavcodec for ffmpeg")
    pkg_check_modules(AVCODEC REQUIRED libavcodec)

    message(STATUS "pkg_check_modules libavformat for ffmpeg")
    pkg_check_modules(AVFORMAT REQUIRED libavformat)

    message(STATUS "pkg_check_modules libavutil for ffmpeg")
    pkg_check_modules(AVUTIL REQUIRED libavutil)

    message(STATUS "pkg_check_modules libswresample for ffmpeg")
    pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

    message(STATUS "pkg_check_modules omxcomponents for raspberrypi")
    pkg_check_modules(OMXCOMPONENTS REQUIRED omxcomponents)

    message(STATUS "pkcg_check_modules bcm_host for raspberrypi")
    pkg_check_modules(BCMHOST REQUIRED bcm_host)

else()
    message(STATUS "skip pkg_check_modules(OMX ...)")
endif()
webos_add_compiler_flags(ALL ${OMX_CFLAGS})

message(STATUS "OMX_CFLAGS : ${OMX_CFLAGS}")
message(STATUS "OMX_LDFLAGS : ${OMX_LDFLAGS}")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/mediaresource
    ${HEADER_ROOT}
    )


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(LIB_CPP_NAME "ndl-directmedia2")
set(TARGET_SRCS
    esplayer-api.cpp
    esplayer.cpp
    message.cpp
    debug.cpp
    parser/parser.cpp
    audioswdecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mediaresource/requestor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omx/omxclient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omx/omxcore.cpp
    )

#FIXME: component-mstar.cpp is just a copy of component-sic.cpp. Fix it for proper operation.
if(DEFINED RPI)
    set(TARGET_SRCS
        ${TARGET_SRCS}
        component-rpi.cpp
        rpiclock.cpp
        )
endif()

add_library(${LIB_CPP_NAME} SHARED ${TARGET_SRCS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -std=c++11")
webos_build_library(NOHEADERS NAME ${LIB_CPP_NAME})


if (DEFINED RPI)
    target_link_libraries (${LIB_CPP_NAME}
        ${OMX_LDFLAGS}
        ${LIBPBNJSON_LDFLAGS}
        ${PMLOGLIB_LDFLAGS}
        ${RESOURCE-CALCULATOR_LDFLAGS}
        ${AVCODEC_LDFLAGS}
        ${AVFORMAT_LDFLAGS}
        ${AVUTIL_LDFLAGS}
        ${SWRESAMPLE_LDFLAGS}
        mdc_client
        mdc_content_provider
        resource_mgr_client
        resource_mgr_client_c
        ${BCMHOST_LDFLAGS}
        ${OMXCOMPONENTS_LDFLAGS}
        )
else()
    target_link_libraries (${LIB_CPP_NAME}
        ${OMX_LDFLAGS}
        ${LIBPBNJSON_LDFLAGS}
        ${PMLOGLIB_LDFLAGS}
        ${RESOURCE-CALCULATOR_LDFLAGS}
        mdc_client
        mdc_content_provider
        resource_mgr_client
        resource_mgr_client_c
        )
endif()
