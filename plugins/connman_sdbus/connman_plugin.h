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

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_PLUGIN_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_PLUGIN_H_

#include <flutter/event_channel.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>
#include <asio/thread_pool.hpp>

#include <memory>

#include "helpers.h"

namespace connman_sdbus {

class ConnmanManager;
class ConnmanAgent;

// ---------------------------------------------------------------------------
// ConnmanPlugin
// ---------------------------------------------------------------------------
// Top-level Flutter plugin class.  Owns:
//   * The single MethodChannel  ("org.automotivelinux.connman")
//   * The single EventChannel   ("org.automotivelinux.connman/events")
//   * A 2-worker asio::thread_pool for all blocking D-Bus method calls
//   * ConnmanManager — wraps net.connman.Manager proxy + service proxies
//   * ConnmanAgent   — D-Bus object at /net/connman/flutter_agent
class ConnmanPlugin final : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

  explicit ConnmanPlugin(flutter::PluginRegistrar* registrar);
  ~ConnmanPlugin() override;

  // Disallow copy and assign.
  ConnmanPlugin(const ConnmanPlugin&) = delete;
  ConnmanPlugin& operator=(const ConnmanPlugin&) = delete;

 private:
  // Fixed 2-worker pool — all blocking D-Bus calls are posted here.
  // Declared first so it outlives manager_ and agent_.
  asio::thread_pool thread_pool_;

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      method_channel_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      event_channel_;

  // Shared event sink — passed to manager and agent for EventChannel emission.
  std::shared_ptr<SharedSink> shared_sink_;

  std::unique_ptr<ConnmanManager> manager_;
  std::unique_ptr<ConnmanAgent> agent_;
};

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_PLUGIN_H_
