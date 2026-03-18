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

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_TECHNOLOGY_HANDLER_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_TECHNOLOGY_HANDLER_H_

#include <flutter/encodable_value.h>
#include <sdbus-c++/sdbus-c++.h>

#include <map>
#include <string>

namespace connman_sdbus {

// ---------------------------------------------------------------------------
// ConnmanTechnologyHandler
// ---------------------------------------------------------------------------
// Pure-virtual interface for technology-specific plugin logic.
//
// Phase 1 supplies WifiTechnologyHandler.  Adding support for ethernet,
// bluetooth, cellular or P2P in Phase 2+ requires only a new handler class;
// ConnmanManager is unchanged.
class ConnmanTechnologyHandler {
 public:
  virtual ~ConnmanTechnologyHandler() = default;

  // Called when ConnMan reports a new technology object.
  // path:  D-Bus object path (e.g. /net/connman/technology/wifi).
  // props: Initial property snapshot from Manager.GetTechnologies().
  virtual void OnTechnologyAdded(
      const sdbus::ObjectPath& path,
      const std::map<std::string, sdbus::Variant>& props) = 0;

  // Called when ConnMan removes a technology (e.g. hardware ejected).
  virtual void OnTechnologyRemoved(const sdbus::ObjectPath& path) = 0;

  // Called when a Technology PropertyChanged signal arrives.
  virtual void OnPropertyChanged(const sdbus::ObjectPath& path,
                                  const std::string& name,
                                  const sdbus::Variant& value) = 0;

  // Snapshot of this technology's properties for getTechnologies /
  // getWifiTechnology MethodChannel responses.
  // Must include keys: path, name, type, powered, connected.
  virtual flutter::EncodableMap GetProperties() const = 0;

  // Technology type string matching ConnMan's Type property
  // (e.g. "wifi", "ethernet", "bluetooth").
  virtual std::string GetType() const = 0;

  // D-Bus object path for this technology.
  virtual sdbus::ObjectPath GetPath() const = 0;
};

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_TECHNOLOGY_HANDLER_H_
