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
project(libndl-directmedia2 C CXX)
include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC1)
webos_component(1 0 0)

message(STATUS "CMAKING... ndl-directmedia2 for ${WEBOS_TARGET_MACHINE}")

set(HEADER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(${WEBOS_TARGET_MACHINE} MATCHES "lm15u")
  message(STATUS "add_definitions( -DLM15U=1 )")
  set(CMAKE_INSTALL_RPATH ${WEBOS_INSTALL_LIBDIR}/openmax)
  set(LM15U 1)
  add_definitions( -DLM15U=1 )
elseif(${WEBOS_TARGET_MACHINE} MATCHES "h15")
  message(STATUS "add_definitions( -DLGSIC=1 )")
  set(LGSIC 1)
  add_definitions( -DLGSIC=1 )
elseif(${WEBOS_TARGET_MACHINE} MATCHES "m14tv")
  message(STATUS "add_definitions( -DLGSIC=1 ) 2nd")
  set(LGSIC 1)
  add_definitions( -DLGSIC=1 )
elseif(${WEBOS_TARGET_MACHINE} MATCHES "m16")
  message(STATUS "add_definitions( -DLGSIC=1 ) 3rd")
  set(LGSIC 1)
  add_definitions( -DLGSIC=1 )
elseif(${WEBOS_TARGET_MACHINE} MATCHES "m16p")
  message(STATUS "add_definitions( -DLGSIC=1 ) 4th")
  set(LGSIC 1)
  add_definitions( -DLGSIC=1 )
elseif(${WEBOS_TARGET_MACHINE} MATCHES "k2l")
  message(STATUS "add_definitions( -DRTK=1 )")
  set(RTK 1)
  add_definitions( -DRTK=1 )
elseif(${WEBOS_TARGET_MACHINE} MATCHES "raspberrypi3")
  message(STATUS "add_definitions( -DRPI=1 )")
  set(RPI 1)
  add_definitions( -DRPI=1 )
else()
  message(STATUS "unsupported machine ${WEBOS_TARGET_MACHINE}")
endif()

if(${WEBOS_TARGET_DISTRO_VARIANT} MATCHES "mini")
  message(STATUS "add_definitions( -DMINI=1 )")
  set(MINI 1)
  add_definitions( -DMINI=1 )
endif()

if(${USE_SVP_SET})
  message(STATUS "add_definitions( -DUSE_SVP=1 )")
  set(USE_SVP 1)
  add_definitions( -DUSE_SVP=1 )
endif()

if(${OMX_SKIP64BIT})
  message(STATUS "add_definitions( -DOMX_SKIP64BIT=1 )")
  set(OMX_SKIP64BIT 1)
  add_definitions( -DOMX_SKIP64BIT=1 )
endif()

# build components
add_subdirectory(src)
add_subdirectory(files/conf)
add_subdirectory(tests)

# webos commands
webos_build_pkgconfig()

# install public header files
install(DIRECTORY include/ndl-directmedia2/ DESTINATION @WEBOS_INSTALL_INCLUDEDIR@/ndl-directmedia2/)
# install luna role file
webos_build_system_bus_files()
