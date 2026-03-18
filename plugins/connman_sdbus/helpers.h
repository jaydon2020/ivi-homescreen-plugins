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

#ifndef FLUTTER_PLUGIN_CONNMAN_SDBUS_HELPERS_H_
#define FLUTTER_PLUGIN_CONNMAN_SDBUS_HELPERS_H_

#include <flutter/encodable_value.h>
#include <flutter/event_sink.h>
#include <sdbus-c++/sdbus-c++.h>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <glib.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "plugins/common/logging.h"

namespace connman_sdbus {

// ---------------------------------------------------------------------------
// Error code constants
// ---------------------------------------------------------------------------

inline constexpr const char* kErrorInvalidArguments = "invalid_arguments";
inline constexpr const char* kErrorDbusError = "dbus_error";
inline constexpr const char* kErrorNotFound = "not_found";
// Must match net.connman.Agent.Error.Canceled so ConnMan re-raises it.
inline constexpr const char* kErrorAgentCanceled =
    "net.connman.Agent.Error.Canceled";

// ---------------------------------------------------------------------------
// SharedSink
// ---------------------------------------------------------------------------
// Thread-safe wrapper around a Flutter EventSink.
//
// Rules:
//   * Always lock `mutex` before reading or writing `sink`.
//   * Pass SharedSink as shared_ptr; capture as weak_ptr in lambdas:
//       auto ws = std::weak_ptr<SharedSink>(shared_sink_ptr);
//       [ws]() {
//           if (auto s = ws.lock()) {
//               std::lock_guard<std::mutex> lk(s->mutex);
//               if (s->sink) s->sink->Success(...);
//           }
//       }
//   * Proxy destruction must happen *outside* any lock on this mutex.
struct SharedSink {
  std::mutex mutex;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> sink;
};

// ---------------------------------------------------------------------------
// GetArgument<T>
// ---------------------------------------------------------------------------
// Extract a typed argument from an EncodableMap by key.
// Returns std::nullopt when key is missing or the value has a different type.
template <typename T>
std::optional<T> GetArgument(const flutter::EncodableMap& args,
                              const std::string& key) {
  auto it = args.find(flutter::EncodableValue(key));
  if (it == args.end()) {
    return std::nullopt;
  }
  const auto* val = std::get_if<T>(&it->second);
  if (val == nullptr) {
    return std::nullopt;
  }
  return *val;
}

// ---------------------------------------------------------------------------
// D-Bus Variant property helpers
// ---------------------------------------------------------------------------
// All D-Bus Variant reads must go through these helpers — no raw .get<T>()
// at call sites.

inline std::string GetStringProp(const sdbus::Variant& v) {
  return v.get<std::string>();
}

inline bool GetBoolProp(const sdbus::Variant& v) {
  return v.get<bool>();
}

// D-Bus Strength is uint8 (type 'y'). EncodableValue has no uint8 variant;
// always widen to int32_t before wrapping.
inline int32_t GetByteProp(const sdbus::Variant& v) {
  return static_cast<int32_t>(v.get<uint8_t>());
}

template <typename T>
std::vector<T> GetArrayProp(const sdbus::Variant& v) {
  return v.get<std::vector<T>>();
}

// ---------------------------------------------------------------------------
// RunOnMainThread
// ---------------------------------------------------------------------------
// Schedule `fn` to be called on the GLib main loop (platform thread).
// Uses g_idle_add — NOT g_main_context_invoke — per ivi-homescreen convention.
inline void RunOnMainThread(std::function<void()> fn) {
  // Heap-allocate so the functor safely crosses thread boundaries.
  auto* fn_ptr = new std::function<void()>(std::move(fn));
  g_idle_add(
      [](gpointer data) -> gboolean {
        auto* f = static_cast<std::function<void()>*>(data);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
      },
      fn_ptr);
}

// ---------------------------------------------------------------------------
// ExecuteAsync
// ---------------------------------------------------------------------------
// Post work onto the bounded thread pool. Never calls thread::detach().
inline void ExecuteAsync(asio::thread_pool& pool, std::function<void()> fn) {
  asio::post(pool, std::move(fn));
}

// ---------------------------------------------------------------------------
// NowMs
// ---------------------------------------------------------------------------
// Current wall-clock time as milliseconds since the Unix epoch.
inline int64_t NowMs() {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

}  // namespace connman_sdbus

#endif  // FLUTTER_PLUGIN_CONNMAN_SDBUS_HELPERS_H_
