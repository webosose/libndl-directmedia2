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


message(STATUS "BUILDING esplayer-test")

# find required packages
include(FindPkgConfig)


pkg_check_modules(GLIB2 REQUIRED glib-2.0)
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS_OTHER})

pkg_check_modules(LUNASERVICE REQUIRED luna-service2)
webos_add_compiler_flags(ALL ${LUNASERVICE_CFLAGS})

if(NOT DEFINED RPI)
pkg_check_modules(OMX REQUIRED openmaxil)
webos_add_compiler_flags(ALL ${OMX_CFLAGS})
endif()

include_directories(
                   ${CMAKE_CURRENT_SOURCE_DIR}
                   ${CMAKE_SOURCE_DIR}/src/
                   ${HEADER_ROOT}
                   )


# create test executable
set(BIN_NAME "esplayer-test")
set(TEST_EXE_SRC
      esplayer-test.cpp
      ffmpegreader.cpp
      esdumpreader.cpp
      lunaservicecall.cpp
      )
add_executable (${BIN_NAME} ${TEST_EXE_SRC})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -std=c++0x -D__STDC_CONSTANT_MACROS")

set_target_properties(${BIN_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries (${BIN_NAME}
                        avformat
                        avcodec
                        avutil
                        ndl-directmedia2
                        pthread
                        dl
                        ${GLIB2_LDFLAGS}
                        ${LUNASERVICE_LDFLAGS}
                        rt
                        pthread
                       )
install(TARGETS ${BIN_NAME} DESTINATION ${WEBOS_INSTALL_BINDIR})

add_executable (esplayer-message-test esplayer-message-test.cpp)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -std=c++0x")
target_link_libraries (esplayer-message-test
                        ndl-directmedia2
                        pthread
                        )

if(NOT DEFINED RPI)
# create unit test executable
webos_use_gtest()
set(BIN_NAME "esplayer-unit-test")
set(TEST_EXE_SRC
    esplayer-unit-test.cpp
    ffmpegreader.cpp
    esdumpreader.cpp
   )
add_executable (${BIN_NAME} ${TEST_EXE_SRC})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -std=c++0x -D__STDC_CONSTANT_MACROS")
set_target_properties(${BIN_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries (${BIN_NAME}
                        avformat
                        avcodec
                        avutil
                        ndl-directmedia2
                        pthread
                        dl
                        ${GLIB2_LDFLAGS}
                        ${LUNASERVICE_LDFLAGS}
                        ${LIBS}
                        ${WEBOS_GTEST_LIBRARIES}
                        rt
                        pthread
                       )
install(TARGETS ${BIN_NAME} DESTINATION ${WEBOS_INSTALL_BINDIR})
endif()
