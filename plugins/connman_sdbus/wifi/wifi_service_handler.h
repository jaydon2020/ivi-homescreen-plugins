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

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_WIFI_WIFI_SERVICE_HANDLER_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_WIFI_WIFI_SERVICE_HANDLER_H_

#include <flutter/encodable_value.h>
#include <sdbus-c++/sdbus-c++.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../generated/service_proxy.h"
#include "../helpers.h"

namespace connman_sdbus {

// ---------------------------------------------------------------------------
// WifiServiceHandler
// ---------------------------------------------------------------------------
// Wraps a single net.connman.Service proxy object.
// Subscribes to PropertyChanged on construction and forwards events to the
// Flutter EventChannel via SharedSink.
//
// One instance lives per wifi service path in ConnmanManager::service_proxies_.
// Instances are created/destroyed dynamically as ServicesChanged diffs arrive.
class WifiServiceHandler final
    : public sdbus::ProxyInterfaces<net::connman::Service_proxy> {
 public:
  WifiServiceHandler(sdbus::IConnection& conn,
                     const sdbus::ObjectPath& path,
                     std::shared_ptr<SharedSink> event_sink);
  ~WifiServiceHandler() override;

  // Disallow copy and assign.
  WifiServiceHandler(const WifiServiceHandler&) = delete;
  WifiServiceHandler& operator=(const WifiServiceHandler&) = delete;

  const sdbus::ObjectPath& GetPath() const { return path_; }

  // Build an EncodableMap snapshot for getWifiServices.
  // Fields: path, name, state, strength (int32), security (List<String>).
  flutter::EncodableMap ToEncodableMap() const;

  // Merge an incoming partial/full property dict (from ServicesChanged).
  void UpdateProperties(const std::map<std::string, sdbus::Variant>& props);

 private:
  // Service_proxy signal override
  void onPropertyChanged(const std::string& name,
                         const sdbus::Variant& value) override;

  void ApplyProperty(const std::string& name, const sdbus::Variant& value);

  sdbus::ObjectPath path_;
  std::weak_ptr<SharedSink> weak_sink_;

  mutable std::mutex props_mutex_;
  std::string name_;
  std::string state_;
  int32_t strength_{0};
  std::vector<std::string> security_;
};

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_WIFI_WIFI_SERVICE_HANDLER_H_
