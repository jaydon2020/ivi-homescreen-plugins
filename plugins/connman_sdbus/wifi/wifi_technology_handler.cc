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

#include "wifi_technology_handler.h"

#include <flutter/encodable_value.h>

#include "plugins/common/logging.h"

namespace connman_sdbus {

WifiTechnologyHandler::WifiTechnologyHandler(
    sdbus::IConnection& conn,
    const sdbus::ObjectPath& path,
    const std::map<std::string, sdbus::Variant>& props,
    std::shared_ptr<SharedSink> event_sink)
    : ProxyInterfaces{conn, sdbus::ServiceName{"net.connman"},
                      sdbus::ObjectPath{path}},
      path_(path),
      weak_sink_(event_sink) {
  registerProxy();

  for (const auto& [name, value] : props) {
    ApplyProperty(name, value);
  }

  SPDLOG_INFO("WifiTechnologyHandler: registered at {}, powered={}, name={}",
              std::string(path), powered_, name_);
}

WifiTechnologyHandler::~WifiTechnologyHandler() {
  unregisterProxy();
}

void WifiTechnologyHandler::OnTechnologyAdded(
    const sdbus::ObjectPath& /* path */,
    const std::map<std::string, sdbus::Variant>& props) {
  for (const auto& [name, value] : props) {
    ApplyProperty(name, value);
  }
}

void WifiTechnologyHandler::OnTechnologyRemoved(
    const sdbus::ObjectPath& /* path */) {
  SPDLOG_INFO("WifiTechnologyHandler: technology removed");
}

void WifiTechnologyHandler::OnPropertyChanged(const sdbus::ObjectPath& path,
                                               const std::string& name,
                                               const sdbus::Variant& value) {
  SPDLOG_DEBUG("WifiTechnologyHandler::OnPropertyChanged path={} name={}",
               std::string(path), name);
  ApplyProperty(name, value);
}

flutter::EncodableMap WifiTechnologyHandler::GetProperties() const {
  std::lock_guard<std::mutex> lk(props_mutex_);
  return flutter::EncodableMap{
      {flutter::EncodableValue("path"),
       flutter::EncodableValue(std::string(path_))},
      {flutter::EncodableValue("name"), flutter::EncodableValue(name_)},
      {flutter::EncodableValue("type"), flutter::EncodableValue(std::string("wifi"))},
      {flutter::EncodableValue("powered"), flutter::EncodableValue(powered_)},
      {flutter::EncodableValue("connected"),
       flutter::EncodableValue(connected_)},
  };
}

void WifiTechnologyHandler::ApplyProperty(const std::string& name,
                                           const sdbus::Variant& value) {
  std::lock_guard<std::mutex> lk(props_mutex_);
  if (name == "Powered") {
    powered_ = GetBoolProp(value);
  } else if (name == "Connected") {
    connected_ = GetBoolProp(value);
  } else if (name == "Name") {
    name_ = GetStringProp(value);
  }
}

void WifiTechnologyHandler::onPropertyChanged(const std::string& name,
                                               const sdbus::Variant& value) {
  SPDLOG_DEBUG("WifiTechnologyHandler::onPropertyChanged name={}", name);

  ApplyProperty(name, value);

  auto sink = weak_sink_.lock();
  if (!sink) {
    return;
  }

  flutter::EncodableMap event{
      {flutter::EncodableValue("type"),
       flutter::EncodableValue(
           std::string("technologyPropertyChanged"))},
      {flutter::EncodableValue("timestamp"),
       flutter::EncodableValue(NowMs())},
      {flutter::EncodableValue("path"),
       flutter::EncodableValue(std::string(path_))},
      {flutter::EncodableValue("name"), flutter::EncodableValue(name)},
  };

  if (name == "Powered" || name == "Connected" || name == "Tethering") {
    event[flutter::EncodableValue("value")] =
        flutter::EncodableValue(GetBoolProp(value));
  } else if (name == "Name" || name == "Type" || name == "TetheringIdentifier" ||
             name == "TetheringPassphrase") {
    event[flutter::EncodableValue("value")] =
        flutter::EncodableValue(GetStringProp(value));
  } else {
    return;
  }

  std::lock_guard<std::mutex> lk(sink->mutex);
  if (sink->sink) {
    sink->sink->Success(flutter::EncodableValue(std::move(event)));
  }
}

}  // namespace connman_sdbus
