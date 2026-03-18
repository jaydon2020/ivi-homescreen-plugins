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

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_WIFI_WIFI_TECHNOLOGY_HANDLER_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_WIFI_WIFI_TECHNOLOGY_HANDLER_H_

#include <flutter/encodable_value.h>
#include <sdbus-c++/sdbus-c++.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "../connman_technology_handler.h"
#include "../generated/technology_proxy.h"
#include "../helpers.h"

namespace connman_sdbus {

// ---------------------------------------------------------------------------
// WifiTechnologyHandler
// ---------------------------------------------------------------------------
// Implements ConnmanTechnologyHandler for the "wifi" technology type.
// Subscribes to net.connman.Technology PropertyChanged for the wifi path and
// emits technologyPropertyChanged events to the EventChannel.
class WifiTechnologyHandler final
    : public ConnmanTechnologyHandler,
      public sdbus::ProxyInterfaces<net::connman::Technology_proxy> {
 public:
  // conn:       System bus connection (shared with ConnmanManager).
  // path:       D-Bus path of the wifi technology object.
  // props:      Initial property snapshot from Manager.GetTechnologies().
  // event_sink: Shared event sink for EventChannel emission.
  WifiTechnologyHandler(sdbus::IConnection& conn,
                        const sdbus::ObjectPath& path,
                        const std::map<std::string, sdbus::Variant>& props,
                        std::shared_ptr<SharedSink> event_sink);
  ~WifiTechnologyHandler() override;

  // Disallow copy and assign.
  WifiTechnologyHandler(const WifiTechnologyHandler&) = delete;
  WifiTechnologyHandler& operator=(const WifiTechnologyHandler&) = delete;

  // ConnmanTechnologyHandler overrides
  void OnTechnologyAdded(
      const sdbus::ObjectPath& path,
      const std::map<std::string, sdbus::Variant>& props) override;
  void OnTechnologyRemoved(const sdbus::ObjectPath& path) override;
  void OnPropertyChanged(const sdbus::ObjectPath& path,
                          const std::string& name,
                          const sdbus::Variant& value) override;
  flutter::EncodableMap GetProperties() const override;
  std::string GetType() const override { return "wifi"; }
  sdbus::ObjectPath GetPath() const override { return path_; }

 private:
  // Technology_proxy signal override (called on D-Bus event loop thread)
  void onPropertyChanged(const std::string& name,
                         const sdbus::Variant& value) override;

  void ApplyProperty(const std::string& name, const sdbus::Variant& value);

  sdbus::ObjectPath path_;
  std::weak_ptr<SharedSink> weak_sink_;

  mutable std::mutex props_mutex_;
  bool powered_{false};
  bool connected_{false};
  std::string name_;
};

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_WIFI_WIFI_TECHNOLOGY_HANDLER_H_
