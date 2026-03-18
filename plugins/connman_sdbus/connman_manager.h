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

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_MANAGER_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_MANAGER_H_

#include <flutter/encodable_value.h>
#include <sdbus-c++/sdbus-c++.h>
#include <asio/thread_pool.hpp>
#include <glib.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "generated/manager_proxy.h"
#include "helpers.h"

namespace connman_sdbus {

class WifiServiceHandler;
class WifiTechnologyHandler;

// ---------------------------------------------------------------------------
// ConnmanManager
// ---------------------------------------------------------------------------
// Wraps the net.connman.Manager D-Bus proxy.
//
// Responsibilities:
//   * Implements all MethodChannel handlers that touch ConnMan state.
//   * Handles TechnologyAdded/Removed signals; delegates to WifiTechnologyHandler.
//   * Handles ServicesChanged: re-diffs the service proxy map on every signal.
//   * Debounces rapid PropertyChanged bursts with a 150 ms GLib timeout before
//     calling GetServices() and pushing a full snapshot to the EventChannel.
class ConnmanManager final
    : public sdbus::ProxyInterfaces<net::connman::Manager_proxy> {
 public:
  static constexpr const char* kService = "net.connman";
  static constexpr const char* kManagerPath = "/";

  ConnmanManager(sdbus::IConnection& conn,
                 std::shared_ptr<SharedSink> event_sink,
                 asio::thread_pool& pool);
  ~ConnmanManager() override;

  // Disallow copy and assign.
  ConnmanManager(const ConnmanManager&) = delete;
  ConnmanManager& operator=(const ConnmanManager&) = delete;

  // MethodChannel handlers — called from thread pool (ExecuteAsync).

  std::string GetState();
  void SetOfflineMode(bool enabled);
  flutter::EncodableList GetTechnologiesEncoded();
  flutter::EncodableMap GetWifiTechnology();
  void SetWifiPowered(bool powered);
  void ScanWifi();
  flutter::EncodableList GetWifiServices();
  void ConnectService(const std::string& path);
  void DisconnectService(const std::string& path);
  void RemoveService(const std::string& path);
  void SetServiceProperty(const std::string& path,
                          const std::string& key,
                          const flutter::EncodableValue& value);

 private:
  // Manager_proxy signal overrides (called on D-Bus event loop thread)
  void onPropertyChanged(const std::string& name,
                         const sdbus::Variant& value) override;
  void onTechnologyAdded(
      const sdbus::ObjectPath& path,
      const std::map<std::string, sdbus::Variant>& properties) override;
  void onTechnologyRemoved(const sdbus::ObjectPath& path) override;
  void onServicesChanged(
      const std::vector<sdbus::Struct<sdbus::ObjectPath,
                                      std::map<std::string, sdbus::Variant>>>&
          changed,
      const std::vector<sdbus::ObjectPath>& removed) override;
  void onPeersChanged(
      const std::vector<sdbus::Struct<sdbus::ObjectPath,
                                      std::map<std::string, sdbus::Variant>>>&
          changed,
      const std::vector<sdbus::ObjectPath>& removed) override;
  void onTetheringClientsChanged(
      const std::vector<std::string>& registered,
      const std::vector<std::string>& removed) override;

  // Service proxy lifecycle — called from D-Bus event loop thread.
  void DiffAndUpdateServiceProxies(
      const std::vector<sdbus::Struct<sdbus::ObjectPath,
                                      std::map<std::string, sdbus::Variant>>>&
          changed,
      const std::vector<sdbus::ObjectPath>& removed);

  // Debounce helpers — all GLib source management is marshalled to the
  // GLib main loop via RunOnMainThread.
  void SchedulePushUpdate();
  void PushUpdate();
  static gboolean DebounceCallback(gpointer data);

  // Convert flutter::EncodableValue to sdbus::Variant for SetServiceProperty.
  static sdbus::Variant ToVariant(const flutter::EncodableValue& val);

  sdbus::IConnection& conn_;
  std::shared_ptr<SharedSink> event_sink_;
  asio::thread_pool& pool_;

  // WiFi technology handler (nullptr until wifi technology is first seen).
  std::unique_ptr<WifiTechnologyHandler> wifi_handler_;

  // Per-wifi-service proxy map — keyed by D-Bus object path.
  mutable std::mutex service_proxies_mutex_;
  std::map<sdbus::ObjectPath, std::unique_ptr<WifiServiceHandler>>
      service_proxies_;

  // GLib timeout source ID for debounced PushUpdate (0 = not scheduled).
  // Only ever read/written on the GLib main loop thread.
  guint debounce_source_id_{0};
};

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_MANAGER_H_
