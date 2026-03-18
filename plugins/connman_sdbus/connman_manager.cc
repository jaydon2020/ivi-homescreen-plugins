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

#include "connman_manager.h"

#include <flutter/encodable_value.h>

#include <set>
#include <stdexcept>

#include "plugins/common/logging.h"
#include "wifi/wifi_service_handler.h"
#include "wifi/wifi_technology_handler.h"

namespace connman_sdbus {

ConnmanManager::ConnmanManager(sdbus::IConnection& conn,
                               std::shared_ptr<SharedSink> event_sink,
                               asio::thread_pool& pool)
    : ProxyInterfaces{conn, sdbus::ServiceName{kService},
                      sdbus::ObjectPath{kManagerPath}},
      conn_(conn),
      event_sink_(std::move(event_sink)),
      pool_(pool) {
  registerProxy();
  SPDLOG_INFO("ConnmanManager: registered proxy at {}", kManagerPath);

  // Bootstrap technology handlers from the initial technology list.
  try {
    const auto techs = net::connman::Manager_proxy::GetTechnologies();
    for (const auto& entry : techs) {
      const auto& path = std::get<0>(entry);
      const auto& props = std::get<1>(entry);

      auto type_it = props.find("Type");
      if (type_it == props.end()) {
        continue;
      }
      const auto type = type_it->second.get<std::string>();
      if (type == "wifi" && !wifi_handler_) {
        wifi_handler_ = std::make_unique<WifiTechnologyHandler>(
            conn, path, props, event_sink_);
        SPDLOG_INFO("ConnmanManager: wifi technology at {}", std::string(path));
      }
    }
  } catch (const sdbus::Error& e) {
    SPDLOG_WARN("ConnmanManager: GetTechnologies failed: {}", e.what());
  }
}

ConnmanManager::~ConnmanManager() {
  // Cancel the debounce source before tearing down — must be on main loop.
  if (debounce_source_id_ != 0) {
    g_source_remove(debounce_source_id_);
    debounce_source_id_ = 0;
  }

  // Destroy service proxies outside any SharedSink lock to prevent deadlock
  // with in-flight PropertyChanged callbacks.
  {
    std::lock_guard<std::mutex> lk(service_proxies_mutex_);
    service_proxies_.clear();
  }

  unregisterProxy();
}

// ---------------------------------------------------------------------------
// MethodChannel handlers
// ---------------------------------------------------------------------------

std::string ConnmanManager::GetState() {
  const auto props = GetProperties();
  auto it = props.find("State");
  if (it == props.end()) {
    return "unknown";
  }
  return GetStringProp(it->second);
}

void ConnmanManager::SetOfflineMode(bool enabled) {
  SetProperty("OfflineMode", sdbus::Variant(enabled));
}

flutter::EncodableList ConnmanManager::GetTechnologiesEncoded() {
  const auto techs =
      ProxyInterfaces<net::connman::Manager_proxy>::GetTechnologies();
  flutter::EncodableList list;
  list.reserve(techs.size());

  for (const auto& entry : techs) {
    const auto& path = std::get<0>(entry);
    const auto& props = std::get<1>(entry);

    flutter::EncodableMap m{
        {flutter::EncodableValue("path"),
         flutter::EncodableValue(std::string(path))},
    };
    for (const auto& [k, v] : props) {
      if (k == "Powered" || k == "Connected" || k == "Tethering") {
        m[flutter::EncodableValue(k)] = flutter::EncodableValue(GetBoolProp(v));
      } else if (k == "Name" || k == "Type") {
        m[flutter::EncodableValue(k)] =
            flutter::EncodableValue(GetStringProp(v));
      }
    }
    list.emplace_back(std::move(m));
  }
  return list;
}

flutter::EncodableMap ConnmanManager::GetWifiTechnology() {
  if (wifi_handler_) {
    return wifi_handler_->GetProperties();
  }
  throw sdbus::Error(sdbus::Error::Name{"net.connman.Error.NotFound"},
                     "wifi technology not present");
}

void ConnmanManager::SetWifiPowered(bool powered) {
  if (!wifi_handler_) {
    throw sdbus::Error(sdbus::Error::Name{"net.connman.Error.NotFound"},
                       "wifi technology not present");
  }
  wifi_handler_->SetProperty("Powered", sdbus::Variant(powered));
}

void ConnmanManager::ScanWifi() {
  if (!wifi_handler_) {
    throw sdbus::Error(sdbus::Error::Name{"net.connman.Error.NotFound"},
                       "wifi technology not present");
  }
  wifi_handler_->Scan();
}

flutter::EncodableList ConnmanManager::GetWifiServices() {
  std::lock_guard<std::mutex> lk(service_proxies_mutex_);
  flutter::EncodableList list;
  list.reserve(service_proxies_.size());
  for (const auto& [path, handler] : service_proxies_) {
    list.emplace_back(handler->ToEncodableMap());
  }
  return list;
}

void ConnmanManager::ConnectService(const std::string& path) {
  std::lock_guard<std::mutex> lk(service_proxies_mutex_);
  auto it = service_proxies_.find(sdbus::ObjectPath(path));
  if (it == service_proxies_.end()) {
    throw sdbus::Error(sdbus::Error::Name{"net.connman.Error.NotFound"},
                       "service not found: " + path);
  }
  it->second->Connect();
}

void ConnmanManager::DisconnectService(const std::string& path) {
  std::lock_guard<std::mutex> lk(service_proxies_mutex_);
  auto it = service_proxies_.find(sdbus::ObjectPath(path));
  if (it == service_proxies_.end()) {
    throw sdbus::Error(sdbus::Error::Name{"net.connman.Error.NotFound"},
                       "service not found: " + path);
  }
  it->second->Disconnect();
}

void ConnmanManager::RemoveService(const std::string& path) {
  std::lock_guard<std::mutex> lk(service_proxies_mutex_);
  auto it = service_proxies_.find(sdbus::ObjectPath(path));
  if (it == service_proxies_.end()) {
    throw sdbus::Error(sdbus::Error::Name{"net.connman.Error.NotFound"},
                       "service not found: " + path);
  }
  it->second->Remove();
}

void ConnmanManager::SetServiceProperty(const std::string& path,
                                         const std::string& key,
                                         const flutter::EncodableValue& value) {
  std::lock_guard<std::mutex> lk(service_proxies_mutex_);
  auto it = service_proxies_.find(sdbus::ObjectPath(path));
  if (it == service_proxies_.end()) {
    throw sdbus::Error(sdbus::Error::Name{"net.connman.Error.NotFound"},
                       "service not found: " + path);
  }
  it->second->SetProperty(key, ToVariant(value));
}

// ---------------------------------------------------------------------------
// Manager_proxy signal overrides
// ---------------------------------------------------------------------------

void ConnmanManager::onPropertyChanged(const std::string& name,
                                        const sdbus::Variant& value) {
  SPDLOG_DEBUG("ConnmanManager::onPropertyChanged name={}", name);

  if (name != "State") {
    return;
  }

  const std::string state = GetStringProp(value);
  std::weak_ptr<SharedSink> ws = event_sink_;

  RunOnMainThread([ws, state]() {
    auto sink = ws.lock();
    if (!sink) {
      return;
    }
    std::lock_guard<std::mutex> lk(sink->mutex);
    if (sink->sink) {
      sink->sink->Success(flutter::EncodableValue(flutter::EncodableMap{
          {flutter::EncodableValue("type"),
           flutter::EncodableValue(std::string("managerStateChanged"))},
          {flutter::EncodableValue("timestamp"),
           flutter::EncodableValue(NowMs())},
          {flutter::EncodableValue("state"), flutter::EncodableValue(state)},
      }));
    }
  });
}

void ConnmanManager::onTechnologyAdded(
    const sdbus::ObjectPath& path,
    const std::map<std::string, sdbus::Variant>& properties) {
  SPDLOG_INFO("ConnmanManager::onTechnologyAdded path={}",
              std::string(path));

  auto type_it = properties.find("Type");
  if (type_it == properties.end()) {
    return;
  }
  const auto type = type_it->second.get<std::string>();

  if (type == "wifi" && !wifi_handler_) {
    wifi_handler_ = std::make_unique<WifiTechnologyHandler>(
        conn_, path, properties, event_sink_);
  } else if (wifi_handler_) {
    wifi_handler_->OnTechnologyAdded(path, properties);
  }
}

void ConnmanManager::onTechnologyRemoved(const sdbus::ObjectPath& path) {
  SPDLOG_INFO("ConnmanManager::onTechnologyRemoved path={}",
              std::string(path));
  if (wifi_handler_ && wifi_handler_->GetPath() == path) {
    wifi_handler_->OnTechnologyRemoved(path);
    wifi_handler_.reset();
  }
}

void ConnmanManager::onServicesChanged(
    const std::vector<sdbus::Struct<sdbus::ObjectPath,
                                    std::map<std::string, sdbus::Variant>>>&
        changed,
    const std::vector<sdbus::ObjectPath>& removed) {
  SPDLOG_DEBUG("ConnmanManager::onServicesChanged changed={} removed={}",
               changed.size(), removed.size());

  DiffAndUpdateServiceProxies(changed, removed);
  SchedulePushUpdate();
}

void ConnmanManager::onPeersChanged(
    const std::vector<
        sdbus::Struct<sdbus::ObjectPath, std::map<std::string, sdbus::Variant>>>&
    /* changed */,
    const std::vector<sdbus::ObjectPath>& /* removed */) {
  // Phase 1: P2P not implemented.
}

void ConnmanManager::onTetheringClientsChanged(
    const std::vector<std::string>& /* registered */,
    const std::vector<std::string>& /* removed */) {
  // Not surfaced to Dart in Phase 1.
}

// ---------------------------------------------------------------------------
// Service proxy diffing
// ---------------------------------------------------------------------------

void ConnmanManager::DiffAndUpdateServiceProxies(
    const std::vector<sdbus::Struct<sdbus::ObjectPath,
                                    std::map<std::string, sdbus::Variant>>>&
        changed,
    const std::vector<sdbus::ObjectPath>& removed) {
  auto& conn = conn_;

  // Collect to-remove paths; destroy proxies *outside* the sink lock.
  std::vector<std::unique_ptr<WifiServiceHandler>> to_destroy;

  {
    std::lock_guard<std::mutex> lk(service_proxies_mutex_);

    // Remove stale proxies.
    for (const auto& path : removed) {
      auto it = service_proxies_.find(path);
      if (it != service_proxies_.end()) {
        to_destroy.push_back(std::move(it->second));
        service_proxies_.erase(it);
        SPDLOG_DEBUG("ConnmanManager: removed service proxy {}",
                     std::string(path));
      }
    }

    // Update or add proxies from `changed`.
    for (const auto& entry : changed) {
      const auto& path = std::get<0>(entry);
      const auto& props = std::get<1>(entry);

      // Only track wifi services (Type == "wifi").
      auto type_it = props.find("Type");
      if (type_it != props.end()) {
        const auto type_val = type_it->second.get<std::string>();
        if (type_val != "wifi") {
          continue;
        }
      }

      auto it = service_proxies_.find(path);
      if (it == service_proxies_.end()) {
        // New service — create proxy and subscribe to PropertyChanged.
        auto handler = std::make_unique<WifiServiceHandler>(
            conn, path, event_sink_);
        handler->UpdateProperties(props);
        service_proxies_.emplace(path, std::move(handler));
        SPDLOG_DEBUG("ConnmanManager: added service proxy {}",
                     std::string(path));
      } else {
        // Existing service — apply partial property updates.
        it->second->UpdateProperties(props);
      }
    }
  }

  // to_destroy goes out of scope here; dtors call unregisterProxy() without
  // holding the SharedSink mutex, preventing deadlock.
}

// ---------------------------------------------------------------------------
// Debounced PushUpdate
// ---------------------------------------------------------------------------

void ConnmanManager::SchedulePushUpdate() {
  // All GLib source management must happen on the GLib main loop thread.
  ConnmanManager* self = this;
  RunOnMainThread([self]() {
    if (self->debounce_source_id_ != 0) {
      g_source_remove(self->debounce_source_id_);
    }
    self->debounce_source_id_ =
        g_timeout_add(150, ConnmanManager::DebounceCallback, self);
  });
}

gboolean ConnmanManager::DebounceCallback(gpointer data) {
  auto* self = static_cast<ConnmanManager*>(data);
  self->debounce_source_id_ = 0;
  self->PushUpdate();
  return G_SOURCE_REMOVE;
}

void ConnmanManager::PushUpdate() {
  // Snapshot service list (acquires service_proxies_mutex_).
  flutter::EncodableList services;
  {
    std::lock_guard<std::mutex> lk(service_proxies_mutex_);
    services.reserve(service_proxies_.size());
    for (const auto& [path, handler] : service_proxies_) {
      services.emplace_back(handler->ToEncodableMap());
    }
  }

  std::weak_ptr<SharedSink> ws = event_sink_;
  RunOnMainThread([ws, svcs = std::move(services)]() mutable {
    auto sink = ws.lock();
    if (!sink) {
      return;
    }
    std::lock_guard<std::mutex> lk(sink->mutex);
    if (sink->sink) {
      sink->sink->Success(flutter::EncodableValue(flutter::EncodableMap{
          {flutter::EncodableValue("type"),
           flutter::EncodableValue(std::string("servicesChanged"))},
          {flutter::EncodableValue("timestamp"),
           flutter::EncodableValue(NowMs())},
          {flutter::EncodableValue("services"),
           flutter::EncodableValue(std::move(svcs))},
      }));
    }
  });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

sdbus::Variant ConnmanManager::ToVariant(const flutter::EncodableValue& val) {
  if (const auto* b = std::get_if<bool>(&val)) {
    return sdbus::Variant(*b);
  }
  if (const auto* i = std::get_if<int32_t>(&val)) {
    return sdbus::Variant(*i);
  }
  if (const auto* i = std::get_if<int64_t>(&val)) {
    return sdbus::Variant(*i);
  }
  if (const auto* d = std::get_if<double>(&val)) {
    return sdbus::Variant(*d);
  }
  if (const auto* s = std::get_if<std::string>(&val)) {
    return sdbus::Variant(*s);
  }
  // Unsupported type — return empty string variant as safe fallback.
  SPDLOG_WARN("ConnmanManager::ToVariant: unsupported EncodableValue type");
  return sdbus::Variant(std::string{});
}

}  // namespace connman_sdbus
