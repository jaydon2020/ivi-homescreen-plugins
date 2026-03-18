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

#include "connman_agent.h"

#include <flutter/encodable_value.h>

#include <string>
#include <utility>

#include "plugins/common/logging.h"
#include "plugins/common/sdbus/sdbus.h"

namespace connman_sdbus {

// ---------------------------------------------------------------------------
// Timeout trampoline state
// ---------------------------------------------------------------------------
struct TimeoutData {
  ConnmanAgent* agent;
  std::string request_id;
};

ConnmanAgent::ConnmanAgent(
    sdbus::IConnection& conn,
    flutter::MethodChannel<flutter::EncodableValue>* channel)
    : AdaptorInterfaces{conn, sdbus::ObjectPath{kAgentPath}},
      channel_(channel) {
  registerAdaptor();
  SPDLOG_INFO("ConnmanAgent: registered D-Bus object at {}", kAgentPath);

  // Register with ConnMan manager.
  try {
    auto mgr = sdbus::createProxy(conn, sdbus::ServiceName{"net.connman"},
                                   sdbus::ObjectPath{"/"});
    mgr->callMethod("RegisterAgent")
        .onInterface("net.connman.Manager")
        .withArguments(sdbus::ObjectPath{kAgentPath});
    SPDLOG_INFO("ConnmanAgent: registered with ConnMan manager");
  } catch (const sdbus::Error& e) {
    SPDLOG_WARN("ConnmanAgent: RegisterAgent failed: {}", e.what());
  }
}

ConnmanAgent::~ConnmanAgent() {
  // Unregister from ConnMan manager (best-effort).
  try {
    auto& conn = plugin_common_sdbus::SystemDBus::Instance().GetConnection();
    auto mgr = sdbus::createProxy(conn, sdbus::ServiceName{"net.connman"},
                                   sdbus::ObjectPath{"/"});
    mgr->callMethod("UnregisterAgent")
        .onInterface("net.connman.Manager")
        .withArguments(sdbus::ObjectPath{kAgentPath});
  } catch (const sdbus::Error& e) {
    SPDLOG_WARN("ConnmanAgent: UnregisterAgent failed: {}", e.what());
  }

  // Drain all pending requests before unregistering the D-Bus object.
  Cancel();

  unregisterAdaptor();
}

// ---------------------------------------------------------------------------
// Public: HandleResponse (called by ConnmanPlugin from platform thread)
// ---------------------------------------------------------------------------

void ConnmanAgent::HandleResponse(const std::string& request_id,
                                   const flutter::EncodableMap& dart_fields) {
  std::lock_guard<std::mutex> lk(pending_mutex_);
  auto it = pending_requests_.find(request_id);
  if (it == pending_requests_.end()) {
    SPDLOG_WARN("ConnmanAgent::HandleResponse: unknown requestId {}",
                request_id);
    return;
  }

  // Cancel the timeout.
  if (it->second.timeout_source_id != 0) {
    g_source_remove(it->second.timeout_source_id);
    it->second.timeout_source_id = 0;
  }

  // Convert Dart EncodableMap → D-Bus variant map.
  std::map<std::string, sdbus::Variant> result_map;
  for (const auto& [key, val] : dart_fields) {
    const auto* k = std::get_if<std::string>(&key);
    const auto* v = std::get_if<std::string>(&val);
    if (k && v) {
      result_map[*k] = sdbus::Variant(*v);
    }
  }

  SPDLOG_INFO("ConnmanAgent: returning credentials for requestId {}",
              request_id);
  it->second.result.returnResults(result_map);
  pending_requests_.erase(it);
}

// ---------------------------------------------------------------------------
// Agent_adaptor overrides
// ---------------------------------------------------------------------------

void ConnmanAgent::Release() {
  SPDLOG_INFO("ConnmanAgent::Release");
  Cancel();
}

void ConnmanAgent::ReportError(const sdbus::ObjectPath& service,
                                const std::string& error) {
  SPDLOG_WARN("ConnmanAgent::ReportError service={} error={}",
              std::string(service), error);
  // Forward to Dart as a fire-and-forget notification (no reply needed).
  channel_->InvokeMethod(
      "reportError",
      std::make_unique<flutter::EncodableValue>(flutter::EncodableMap{
          {flutter::EncodableValue("service"),
           flutter::EncodableValue(std::string(service))},
          {flutter::EncodableValue("error"), flutter::EncodableValue(error)},
      }));
}

void ConnmanAgent::RequestBrowser(const sdbus::ObjectPath& service,
                                   const std::string& url) {
  SPDLOG_INFO("ConnmanAgent::RequestBrowser service={} url={}",
              std::string(service), url);
  channel_->InvokeMethod(
      "requestBrowser",
      std::make_unique<flutter::EncodableValue>(flutter::EncodableMap{
          {flutter::EncodableValue("service"),
           flutter::EncodableValue(std::string(service))},
          {flutter::EncodableValue("url"), flutter::EncodableValue(url)},
      }));
}

void ConnmanAgent::RequestInput(
    sdbus::Result<std::map<std::string, sdbus::Variant>>&& result,
    sdbus::ObjectPath service,
    std::map<std::string, sdbus::Variant> fields) {
  std::string request_id;
  {
    std::lock_guard<std::mutex> lk(pending_mutex_);
    request_id = std::to_string(++request_counter_);
  }

  SPDLOG_INFO("ConnmanAgent::RequestInput service={} requestId={}",
              std::string(service), request_id);

  // Build the field descriptors for Dart.
  // Each field carries: type, requirement, alternates (optional), value (optional).
  flutter::EncodableMap field_map;
  for (const auto& [fname, fval] : fields) {
    const auto& inner = fval.get<std::map<std::string, sdbus::Variant>>();
    flutter::EncodableMap desc;

    auto it_type = inner.find("Type");
    if (it_type != inner.end()) {
      desc[flutter::EncodableValue("type")] =
          flutter::EncodableValue(GetStringProp(it_type->second));
    }
    auto it_req = inner.find("Requirement");
    if (it_req != inner.end()) {
      desc[flutter::EncodableValue("requirement")] =
          flutter::EncodableValue(GetStringProp(it_req->second));
    }
    auto it_alt = inner.find("Alternates");
    if (it_alt != inner.end()) {
      flutter::EncodableList alts;
      for (const auto& a : GetArrayProp<std::string>(it_alt->second)) {
        alts.emplace_back(a);
      }
      desc[flutter::EncodableValue("alternates")] =
          flutter::EncodableValue(std::move(alts));
    }
    auto it_val = inner.find("Value");
    if (it_val != inner.end()) {
      desc[flutter::EncodableValue("value")] =
          flutter::EncodableValue(GetStringProp(it_val->second));
    }
    field_map[flutter::EncodableValue(fname)] =
        flutter::EncodableValue(std::move(desc));
  }

  // Store pending result and start timeout.
  auto* td = new TimeoutData{this, request_id};
  guint src_id = g_timeout_add_seconds(
      kTimeoutSeconds,
      [](gpointer data) -> gboolean {
        auto* tdata = static_cast<TimeoutData*>(data);
        tdata->agent->ExpireRequest(tdata->request_id);
        delete tdata;
        return G_SOURCE_REMOVE;
      },
      td);

  {
    std::lock_guard<std::mutex> lk(pending_mutex_);
    pending_requests_[request_id] = PendingRequest{std::move(result), src_id};
  }

  // Notify Dart.
  channel_->InvokeMethod(
      "requestInput",
      std::make_unique<flutter::EncodableValue>(flutter::EncodableMap{
          {flutter::EncodableValue("requestId"),
           flutter::EncodableValue(request_id)},
          {flutter::EncodableValue("service"),
           flutter::EncodableValue(std::string(service))},
          {flutter::EncodableValue("fields"),
           flutter::EncodableValue(std::move(field_map))},
      }));
}

void ConnmanAgent::Cancel() {
  SPDLOG_INFO("ConnmanAgent::Cancel: draining {} pending request(s)",
              pending_requests_.size());

  std::map<std::string, PendingRequest> to_cancel;
  {
    std::lock_guard<std::mutex> lk(pending_mutex_);
    to_cancel = std::move(pending_requests_);
  }

  for (auto& [id, req] : to_cancel) {
    if (req.timeout_source_id != 0) {
      g_source_remove(req.timeout_source_id);
    }
    req.result.returnError(
        sdbus::Error{sdbus::Error::Name{kErrorAgentCanceled},
                     "agent canceled"});
    SPDLOG_DEBUG("ConnmanAgent::Cancel: expired requestId {}", id);
  }
}

// ---------------------------------------------------------------------------
// Private: ExpireRequest (GLib timeout callback path)
// ---------------------------------------------------------------------------

void ConnmanAgent::ExpireRequest(const std::string& request_id) {
  PendingRequest req;
  bool found = false;
  {
    std::lock_guard<std::mutex> lk(pending_mutex_);
    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
      req = std::move(it->second);
      req.timeout_source_id = 0;  // already firing
      pending_requests_.erase(it);
      found = true;
    }
  }

  if (found) {
    SPDLOG_WARN("ConnmanAgent: request {} timed out after {}s", request_id,
                kTimeoutSeconds);
    req.result.returnError(
        sdbus::Error{sdbus::Error::Name{kErrorAgentCanceled},
                     "agent request timed out"});
  }
}

}  // namespace connman_sdbus
