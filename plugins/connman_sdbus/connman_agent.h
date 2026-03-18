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

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_AGENT_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_AGENT_H_

#include <flutter/encodable_value.h>
#include <flutter/method_channel.h>
#include <sdbus-c++/sdbus-c++.h>
#include <glib.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "generated/agent_adaptor.h"
#include "helpers.h"

namespace connman_sdbus {

// ---------------------------------------------------------------------------
// ConnmanAgent
// ---------------------------------------------------------------------------
// Implements the net.connman.Agent D-Bus interface and registers at
// /net/connman/flutter_agent.
//
// Flow for credential input:
//   1. ConnMan calls RequestInput(service, fields).
//   2. Agent assigns a requestId (monotonic counter), stores the pending
//      sdbus::Result keyed by requestId, and arms a 60-second GLib timeout.
//   3. Agent invokes "requestInput" on the Flutter MethodChannel carrying
//      {requestId, servicePath, fields}.  Fields include type, requirement,
//      alternates (if any), and value (if pre-filled).
//   4. Dart shows UI, then calls agentResponse({requestId, fields}) on the
//      MethodChannel.
//   5. ConnmanPlugin calls HandleResponse(requestId, fields).
//   6. Agent looks up the pending result, cancels the timeout, converts the
//      Dart fields map to a D-Bus variant map, and calls result.returnResults().
//
// Timeout path: GLib fires the timeout callback → returnError(Canceled) +
//   remove from map.  No thread is detached.
//
// Cancel(): drains all pending requests (not just the most recent).
class ConnmanAgent final
    : public sdbus::AdaptorInterfaces<net::connman::Agent_adaptor> {
 public:
  static constexpr const char* kAgentPath = "/net/connman/flutter_agent";
  static constexpr int kTimeoutSeconds = 60;

  // channel: Must outlive ConnmanAgent.  ConnmanPlugin owns both.
  ConnmanAgent(sdbus::IConnection& conn,
               flutter::MethodChannel<flutter::EncodableValue>* channel);
  ~ConnmanAgent() override;

  // Disallow copy and assign.
  ConnmanAgent(const ConnmanAgent&) = delete;
  ConnmanAgent& operator=(const ConnmanAgent&) = delete;

  // Called by ConnmanPlugin when Dart calls agentResponse on the MethodChannel.
  void HandleResponse(const std::string& request_id,
                      const flutter::EncodableMap& dart_fields);

 private:
  // Agent_adaptor pure-virtual overrides
  void Release() override;
  void ReportError(const sdbus::ObjectPath& service,
                   const std::string& error) override;
  void RequestBrowser(const sdbus::ObjectPath& service,
                      const std::string& url) override;
  void RequestInput(
      sdbus::Result<std::map<std::string, sdbus::Variant>>&& result,
      sdbus::ObjectPath service,
      std::map<std::string, sdbus::Variant> fields) override;
  void Cancel() override;

  // Per-pending-request state.
  struct PendingRequest {
    sdbus::Result<std::map<std::string, sdbus::Variant>> result;
    guint timeout_source_id{0};
  };

  // Expire a single request: fire returnError and erase from map.
  // Must be called with pending_mutex_ held or from the timeout callback
  // (which re-acquires the lock).
  void ExpireRequest(const std::string& request_id);

  static gboolean TimeoutCallback(gpointer data);

  flutter::MethodChannel<flutter::EncodableValue>* channel_;

  std::mutex pending_mutex_;
  std::map<std::string, PendingRequest> pending_requests_;
  uint64_t request_counter_{0};
};

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_CONNMAN_AGENT_H_
