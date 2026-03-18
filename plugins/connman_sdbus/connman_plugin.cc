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

#include "connman_plugin.h"

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>
#include <flutter/standard_method_codec.h>

#include <memory>

#include "connman_agent.h"
#include "connman_manager.h"
#include "messages.g.h"
#include "plugins/common/logging.h"
#include "plugins/common/sdbus/sdbus.h"

namespace connman_sdbus {

namespace {
static constexpr const char* kMethodChannelName = "org.automotivelinux.connman";
static constexpr const char* kEventChannelName =
    "org.automotivelinux.connman/events";
}  // namespace

// ---------------------------------------------------------------------------
// static
// ---------------------------------------------------------------------------

void ConnmanPlugin::RegisterWithRegistrar(flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<ConnmanPlugin>(registrar);
  registrar->AddPlugin(std::move(plugin));
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ConnmanPlugin::ConnmanPlugin(flutter::PluginRegistrar* registrar)
    : thread_pool_(2),  // bounded: 2 workers for all blocking D-Bus calls
      shared_sink_(std::make_shared<SharedSink>()) {
  auto& conn = plugin_common_sdbus::SystemDBus::Instance().GetConnection();

  // 1. Manager first — no channel dependency.
  manager_ = std::make_unique<ConnmanManager>(conn, shared_sink_, thread_pool_);

  // 2. Channel — stable pointer; never replaced after this point.
  method_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), kMethodChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  // 3. Agent — holds method_channel_.get(); channel must already exist.
  agent_ = std::make_unique<ConnmanAgent>(conn, method_channel_.get());

  // 4. Wire the dispatch handler — both manager_ and agent_ alive.
  SetUpMethodChannel(*method_channel_, manager_.get(), agent_.get(),
                     thread_pool_);

  // 4. Set up EventChannel.
  event_channel_ =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          registrar->messenger(), kEventChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  std::shared_ptr<SharedSink>* sink_ptr = &shared_sink_;
  auto stream_handler =
      std::make_unique<flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
          [sink_ptr](
              const flutter::EncodableValue* /* args */,
              std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&&
                  sink) {
            std::lock_guard<std::mutex> lk((*sink_ptr)->mutex);
            (*sink_ptr)->sink = std::move(sink);
            SPDLOG_INFO("connman_sdbus: EventChannel onListen");
            return nullptr;
          },
          [sink_ptr](const flutter::EncodableValue* /* args */) {
            std::lock_guard<std::mutex> lk((*sink_ptr)->mutex);
            (*sink_ptr)->sink.reset();
            SPDLOG_INFO("connman_sdbus: EventChannel onCancel");
            return nullptr;
          });

  event_channel_->SetStreamHandler(std::move(stream_handler));

  SPDLOG_INFO("ConnmanPlugin: initialised (pool=2, method={}, event={})",
              kMethodChannelName, kEventChannelName);
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

ConnmanPlugin::~ConnmanPlugin() {
  // Destroy agent before manager (agent holds a raw pointer to method_channel_
  // and may reference manager state during shutdown).
  agent_.reset();
  manager_.reset();
  // Join the thread pool — waits for any in-flight D-Bus tasks to finish.
  thread_pool_.join();
}

}  // namespace connman_sdbus
