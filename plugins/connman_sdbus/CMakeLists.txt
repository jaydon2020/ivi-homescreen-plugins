#
# Copyright 2020-2025 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.16)
project(connman_sdbus_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)

# asio (header-only, bundled with ivi-homescreen or available as system package)
find_package(asio QUIET)
if(NOT asio_FOUND)
    pkg_check_modules(ASIO IMPORTED_TARGET asio)
endif()

add_library(plugin_connman_sdbus STATIC
        connman_plugin_c_api.cc
        connman_plugin.cc
        connman_manager.cc
        connman_agent.cc
        messages.g.cc
        wifi/wifi_technology_handler.cc
        wifi/wifi_service_handler.cc
)

target_include_directories(plugin_connman_sdbus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(plugin_connman_sdbus PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

target_link_libraries(plugin_connman_sdbus PUBLIC
    PkgConfig::GLIB
    plugin_common
    platform_homescreen
)

# asio linkage — prefer package, fall back to include-only
if(TARGET asio::asio)
    target_link_libraries(plugin_connman_sdbus PUBLIC asio::asio)
elseif(ASIO_FOUND)
    target_link_libraries(plugin_connman_sdbus PUBLIC PkgConfig::ASIO)
else()
    message(STATUS "connman_sdbus: using header-only asio from system includes")
    target_compile_definitions(plugin_connman_sdbus PUBLIC ASIO_STANDALONE)
endif()

if(BUILD_UNIT_TESTS_CONNMAN)
    add_subdirectory(test)
endif()

install(TARGETS plugin_connman_sdbus
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
