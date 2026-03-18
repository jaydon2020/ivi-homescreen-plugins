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

find_package(GTest REQUIRED)

add_executable(test_connman
    test_connman.cc
)

target_include_directories(test_connman PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(test_connman PRIVATE
    GTest::GTest
    GTest::Main
    GTest::gmock
    plugin_connman_sdbus
    plugin_common
)

target_compile_options(test_connman PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

include(GoogleTest)
gtest_discover_tests(test_connman)
