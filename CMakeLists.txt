# Copyright (c) 2013-2019 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 2.8.7)

project(notification CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC4)
webos_component(1 0 0)

webos_machine_dep()

include_directories(inc)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

pkg_check_modules(LUNASERVICE REQUIRED luna-service2)
include_directories(${LUNASERVICE_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE_CFLAGS})

pkg_check_modules(PBNJSON_CPP REQUIRED pbnjson_cpp)
include_directories(${PBNJSON_CPP_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PBNJSON_CPP_CFLAGS_OTHER})

pkg_check_modules(PMLOG REQUIRED PmLogLib)
include_directories(${PMLOG_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMLOG_CFLAGS_OTHER})

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${Boost_CFLAGS_OTHER})
add_definitions(-DBOOST_BIND_NO_PLACEHOLDERS)

# -- check for libxml++
pkg_check_modules(LIBXMLXX REQUIRED libxml++-2.6)
webos_add_compiler_flags(ALL ${LIBXMLXX_CFLAGS})

pkg_check_modules(OPENSSL REQUIRED openssl)
webos_add_compiler_flags(ALL ${OPENSSL_CFLAGS})

webos_add_compiler_flags(ALL -Wall "-std=c++11")

webos_add_linker_options(ALL --no-undefined)

webos_configure_source_files(cfg ${PROJECT_SOURCE_DIR}/src/Settings.h)
webos_configure_source_files(cfg ${PROJECT_SOURCE_DIR}/src/JUtil.h)

include_directories(${PROJECT_BINARY_DIR}/Configured/src)
include_directories(src)

file (GLOB_RECURSE SOURCES src/*.cpp)
add_executable(notificationmgr ${SOURCES})

target_link_libraries(notificationmgr
    ${GLIB2_LDFLAGS}
    ${LUNASERVICE_LDFLAGS}
    ${PBNJSON_CPP_LDFLAGS}
    ${PMLOG_LDFLAGS}
    ${LIBXMLXX_LDFLAGS}
    ${OPENSSL_LDFLAGS}
)

file(GLOB_RECURSE SCHEMAS files/schema/*.schema)
install(FILES ${SCHEMAS} DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/schemas/notificationmgr)

file(GLOB_RECURSE IMAGES files/images/*.png)
install(FILES ${IMAGES} DESTINATION ${WEBOS_INSTALL_WEBOS_PREFIX}/notificationmgr/images)

webos_configure_source_files(confFile files/conf/config.json)
install(PROGRAMS ${confFile} DESTINATION ${WEBOS_INSTALL_WEBOS_PREFIX}/notificationmgr)

webos_build_daemon(NAME notificationmgr LAUNCH files/launch)
webos_build_system_bus_files()
webos_build_db8_files()
