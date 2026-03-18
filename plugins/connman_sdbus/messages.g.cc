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

// messages.g.cc — MethodChannel dispatch table, nothing else.
// All business logic lives in ConnmanManager / ConnmanAgent.

#include "messages.g.h"

#include <flutter/method_channel.h>

#include <functional>
#include <memory>
#include <string>

#include "connman_agent.h"
#include "connman_manager.h"
#include "helpers.h"
#include "plugins/common/logging.h"

namespace connman_sdbus {

namespace {

// Helper: reply success with null.
void ReplyNull(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>& result) {
  result->Success(flutter::EncodableValue());
}

// Helper: reply success with a value.
void ReplyValue(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>& result,
    flutter::EncodableValue val) {
  result->Success(std::move(val));
}

}  // namespace

void SetUpMethodChannel(
    flutter::MethodChannel<flutter::EncodableValue>& channel,
    ConnmanManager* manager,
    ConnmanAgent* agent,
    asio::thread_pool& pool) {
  channel.SetMethodCallHandler(
      [manager, agent, &pool](
          const flutter::MethodCall<flutter::EncodableValue>& call,
          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
              result) {
        const auto& method = call.method_name();
        const auto* args_val = call.arguments();
        const flutter::EncodableMap* args =
            args_val ? std::get_if<flutter::EncodableMap>(args_val) : nullptr;

        SPDLOG_DEBUG("connman_sdbus: method={}", method);

        // --- Fast synchronous methods (no D-Bus blocking) ---

        if (method == "getManagerState") {
          ExecuteAsync(pool, [manager, res = std::move(result)]() mutable {
            try {
              res->Success(flutter::EncodableValue(manager->GetState()));
            } catch (const sdbus::Error& e) {
              res->Error(kErrorDbusError, e.what());
            }
          });
          return;
        }

        if (method == "setOfflineMode") {
          if (!args) {
            result->Error(kErrorInvalidArguments, "expected map");
            return;
          }
          auto enabled = GetArgument<bool>(*args, "enabled");
          if (!enabled.has_value()) {
            result->Error(kErrorInvalidArguments, "missing 'enabled' bool");
            return;
          }
          ExecuteAsync(pool,
                       [manager, en = *enabled, res = std::move(result)]() mutable {
                         try {
                           manager->SetOfflineMode(en);
                           res->Success(flutter::EncodableValue());
                         } catch (const sdbus::Error& e) {
                           res->Error(kErrorDbusError, e.what());
                         }
                       });
          return;
        }

        if (method == "getTechnologies") {
          ExecuteAsync(pool, [manager, res = std::move(result)]() mutable {
            try {
              res->Success(
                  flutter::EncodableValue(manager->GetTechnologiesEncoded()));
            } catch (const sdbus::Error& e) {
              res->Error(kErrorDbusError, e.what());
            }
          });
          return;
        }

        if (method == "getWifiTechnology") {
          ExecuteAsync(pool, [manager, res = std::move(result)]() mutable {
            try {
              res->Success(
                  flutter::EncodableValue(manager->GetWifiTechnology()));
            } catch (const sdbus::Error& e) {
              res->Error(kErrorNotFound, e.what());
            }
          });
          return;
        }

        if (method == "setWifiPowered") {
          if (!args) {
            result->Error(kErrorInvalidArguments, "expected map");
            return;
          }
          auto powered = GetArgument<bool>(*args, "powered");
          if (!powered.has_value()) {
            result->Error(kErrorInvalidArguments, "missing 'powered' bool");
            return;
          }
          ExecuteAsync(pool,
                       [manager, pw = *powered, res = std::move(result)]() mutable {
                         try {
                           manager->SetWifiPowered(pw);
                           res->Success(flutter::EncodableValue());
                         } catch (const sdbus::Error& e) {
                           res->Error(kErrorDbusError, e.what());
                         }
                       });
          return;
        }

        if (method == "scanWifi") {
          ExecuteAsync(pool, [manager, res = std::move(result)]() mutable {
            try {
              manager->ScanWifi();
              res->Success(flutter::EncodableValue());
            } catch (const sdbus::Error& e) {
              res->Error(kErrorDbusError, e.what());
            }
          });
          return;
        }

        if (method == "getWifiServices") {
          ExecuteAsync(pool, [manager, res = std::move(result)]() mutable {
            try {
              res->Success(
                  flutter::EncodableValue(manager->GetWifiServices()));
            } catch (const sdbus::Error& e) {
              res->Error(kErrorDbusError, e.what());
            }
          });
          return;
        }

        if (method == "connectService") {
          if (!args) {
            result->Error(kErrorInvalidArguments, "expected map");
            return;
          }
          auto path = GetArgument<std::string>(*args, "path");
          if (!path.has_value()) {
            result->Error(kErrorInvalidArguments, "missing 'path' string");
            return;
          }
          ExecuteAsync(pool,
                       [manager, p = std::move(*path),
                        res = std::move(result)]() mutable {
                         try {
                           manager->ConnectService(p);
                           res->Success(flutter::EncodableValue());
                         } catch (const sdbus::Error& e) {
                           res->Error(kErrorDbusError, e.what());
                         }
                       });
          return;
        }

        if (method == "disconnectService") {
          if (!args) {
            result->Error(kErrorInvalidArguments, "expected map");
            return;
          }
          auto path = GetArgument<std::string>(*args, "path");
          if (!path.has_value()) {
            result->Error(kErrorInvalidArguments, "missing 'path' string");
            return;
          }
          ExecuteAsync(pool,
                       [manager, p = std::move(*path),
                        res = std::move(result)]() mutable {
                         try {
                           manager->DisconnectService(p);
                           res->Success(flutter::EncodableValue());
                         } catch (const sdbus::Error& e) {
                           res->Error(kErrorDbusError, e.what());
                         }
                       });
          return;
        }

        if (method == "removeService") {
          if (!args) {
            result->Error(kErrorInvalidArguments, "expected map");
            return;
          }
          auto path = GetArgument<std::string>(*args, "path");
          if (!path.has_value()) {
            result->Error(kErrorInvalidArguments, "missing 'path' string");
            return;
          }
          ExecuteAsync(pool,
                       [manager, p = std::move(*path),
                        res = std::move(result)]() mutable {
                         try {
                           manager->RemoveService(p);
                           res->Success(flutter::EncodableValue());
                         } catch (const sdbus::Error& e) {
                           res->Error(kErrorDbusError, e.what());
                         }
                       });
          return;
        }

        if (method == "setServiceProperty") {
          if (!args) {
            result->Error(kErrorInvalidArguments, "expected map");
            return;
          }
          auto path = GetArgument<std::string>(*args, "path");
          auto key = GetArgument<std::string>(*args, "key");
          if (!path.has_value() || !key.has_value()) {
            result->Error(kErrorInvalidArguments,
                          "missing 'path' or 'key' string");
            return;
          }
          auto val_it = args->find(flutter::EncodableValue("value"));
          if (val_it == args->end()) {
            result->Error(kErrorInvalidArguments, "missing 'value'");
            return;
          }
          flutter::EncodableValue val = val_it->second;
          ExecuteAsync(pool,
                       [manager, p = std::move(*path), k = std::move(*key),
                        v = std::move(val), res = std::move(result)]() mutable {
                         try {
                           manager->SetServiceProperty(p, k, v);
                           res->Success(flutter::EncodableValue());
                         } catch (const sdbus::Error& e) {
                           res->Error(kErrorDbusError, e.what());
                         }
                       });
          return;
        }

        if (method == "agentResponse") {
          if (!args) {
            result->Error(kErrorInvalidArguments, "expected map");
            return;
          }
          auto request_id = GetArgument<std::string>(*args, "requestId");
          if (!request_id.has_value()) {
            result->Error(kErrorInvalidArguments, "missing 'requestId'");
            return;
          }
          auto fields_it = args->find(flutter::EncodableValue("fields"));
          const flutter::EncodableMap* fields =
              (fields_it != args->end())
                  ? std::get_if<flutter::EncodableMap>(&fields_it->second)
                  : nullptr;
          if (!fields) {
            result->Error(kErrorInvalidArguments,
                          "missing or invalid 'fields' map");
            return;
          }
          agent->HandleResponse(*request_id, *fields);
          result->Success(flutter::EncodableValue());
          return;
        }

        result->NotImplemented();
      });
}

}  // namespace connman_sdbus
