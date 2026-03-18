/*
 * Copyright 2020-2025 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_MESSAGES_G_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_MESSAGES_G_H_

#include <flutter/encodable_value.h>
#include <flutter/method_channel.h>
#include <asio/thread_pool.hpp>

namespace connman_sdbus {

class ConnmanManager;
class ConnmanAgent;

// SetUpMethodChannel
// ------------------
// Wires the dispatch handler onto an already-created MethodChannel.
// The caller owns the channel and must ensure it outlives the handler.
//
// The handler runs on the Flutter platform thread.  Blocking D-Bus methods
// are dispatched to `pool` via ExecuteAsync.
void SetUpMethodChannel(
    flutter::MethodChannel<flutter::EncodableValue>& channel,
    ConnmanManager* manager,
    ConnmanAgent* agent,
    asio::thread_pool& pool);

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_MESSAGES_G_H_
