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

#include "wifi_service_handler.h"

#include <flutter/encodable_value.h>

#include <string>
#include <vector>

#include "plugins/common/logging.h"

namespace connman_sdbus {

namespace {
// D-Bus service name for ConnMan — matches ConnmanManager::kService.
constexpr const char* kConnmanService = "net.connman";
}  // namespace

WifiServiceHandler::WifiServiceHandler(sdbus::IConnection& conn,
                                       const sdbus::ObjectPath& path,
                                       std::shared_ptr<SharedSink> event_sink)
    : ProxyInterfaces{conn, sdbus::ServiceName{kConnmanService},
                      sdbus::ObjectPath{path}},
      path_(path),
      weak_sink_(event_sink) {
  registerProxy();
  SPDLOG_DEBUG("WifiServiceHandler: subscribed to {}", std::string(path));
}

WifiServiceHandler::~WifiServiceHandler() {
  unregisterProxy();
}

flutter::EncodableMap WifiServiceHandler::ToEncodableMap() const {
  std::lock_guard<std::mutex> lk(props_mutex_);

  flutter::EncodableList sec_list;
  sec_list.reserve(security_.size());
  for (const auto& s : security_) {
    sec_list.emplace_back(s);
  }

  return flutter::EncodableMap{
      {flutter::EncodableValue("path"),
       flutter::EncodableValue(std::string(path_))},
      {flutter::EncodableValue("name"), flutter::EncodableValue(name_)},
      {flutter::EncodableValue("state"), flutter::EncodableValue(state_)},
      // strength is uint8 from D-Bus — stored as int32_t per type-safety rules.
      {flutter::EncodableValue("strength"),
       flutter::EncodableValue(strength_)},
      {flutter::EncodableValue("security"),
       flutter::EncodableValue(std::move(sec_list))},
  };
}

void WifiServiceHandler::UpdateProperties(
    const std::map<std::string, sdbus::Variant>& props) {
  for (const auto& [name, value] : props) {
    ApplyProperty(name, value);
  }
}

void WifiServiceHandler::ApplyProperty(const std::string& name,
                                        const sdbus::Variant& value) {
  std::lock_guard<std::mutex> lk(props_mutex_);
  if (name == "Name") {
    name_ = GetStringProp(value);
  } else if (name == "State") {
    state_ = GetStringProp(value);
  } else if (name == "Strength") {
    strength_ = GetByteProp(value);
  } else if (name == "Security") {
    security_ = GetArrayProp<std::string>(value);
  }
}

void WifiServiceHandler::onPropertyChanged(const std::string& name,
                                            const sdbus::Variant& value) {
  SPDLOG_DEBUG("WifiServiceHandler::onPropertyChanged path={} name={}",
               std::string(path_), name);

  ApplyProperty(name, value);

  auto sink = weak_sink_.lock();
  if (!sink) {
    return;
  }

  flutter::EncodableMap event{
      {flutter::EncodableValue("type"),
       flutter::EncodableValue(std::string("servicePropertyChanged"))},
      {flutter::EncodableValue("timestamp"),
       flutter::EncodableValue(NowMs())},
      {flutter::EncodableValue("path"),
       flutter::EncodableValue(std::string(path_))},
      {flutter::EncodableValue("name"), flutter::EncodableValue(name)},
  };

  // Emit typed value based on the property changed.
  if (name == "Strength") {
    event[flutter::EncodableValue("value")] =
        flutter::EncodableValue(GetByteProp(value));
  } else if (name == "State" || name == "Name" || name == "Error") {
    event[flutter::EncodableValue("value")] =
        flutter::EncodableValue(GetStringProp(value));
  } else if (name == "Favorite" || name == "AutoConnect" || name == "Roaming" ||
             name == "Immutable") {
    event[flutter::EncodableValue("value")] =
        flutter::EncodableValue(GetBoolProp(value));
  } else {
    // Generic: skip unknown properties (no raw variant exposure to Dart).
    return;
  }

  std::lock_guard<std::mutex> lk(sink->mutex);
  if (sink->sink) {
    sink->sink->Success(flutter::EncodableValue(std::move(event)));
  }
}

}  // namespace connman_sdbus
