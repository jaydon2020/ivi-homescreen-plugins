# plugin_connman_sdbus

Flutter plugin for **AGL IVI-Homescreen** that wraps the [ConnMan](https://01.org/connman)
network manager via D-Bus using [sdbus-c++ v2.x](https://github.com/Kistler-Group/sdbus-cpp).

Copyright 2020-2025 Toyota Connected North America — Apache 2.0 License.

---

## Architecture

```
Dart (Flutter)
    │  MethodChannel: org.automotivelinux.connman
    │  EventChannel:  org.automotivelinux.connman/events
    ▼
ConnmanPlugin (connman_plugin.cc)
    ├── ConnmanManager (connman_manager.cc)
    │     ├── Manager_proxy  (net.connman.Manager @ /)
    │     ├── WifiTechnologyHandler
    │     │     └── Technology_proxy  (net.connman.Technology @ /…/wifi)
    │     └── WifiServiceHandler  [0..N, dynamic]
    │           └── Service_proxy  (net.connman.Service @ /…/wifi_…)
    └── ConnmanAgent (connman_agent.cc)
          └── Agent_adaptor  (net.connman.Agent @ /net/connman/flutter_agent)

Threading
  [Flutter platform / GLib main loop]
       ↑ g_idle_add (RunOnMainThread)
  [D-Bus event loop]   ← sdbus-c++ enterEventLoopAsync()
       → asio::post
  [Worker pool (2 threads)]  ← all blocking D-Bus method calls
```

---

## MethodChannel API  `org.automotivelinux.connman`

| Method | Arguments | Return | Notes |
|---|---|---|---|
| `getManagerState` | — | `String` | offline/idle/ready/online |
| `setOfflineMode` | `{enabled: bool}` | `null` | |
| `getTechnologies` | — | `List<Map>` | all types |
| `getWifiTechnology` | — | `Map` | powered, connected, type, name, path |
| `setWifiPowered` | `{powered: bool}` | `null` | |
| `scanWifi` | — | `null` | blocks until scan completes |
| `getWifiServices` | — | `List<Map>` | path, name, state, strength(int), security |
| `connectService` | `{path: String}` | `null` | |
| `disconnectService` | `{path: String}` | `null` | |
| `removeService` | `{path: String}` | `null` | |
| `setServiceProperty` | `{path, key, value}` | `null` | |
| `agentResponse` | `{requestId: String, fields: Map}` | `null` | ID-keyed reply |

**Error codes**: `invalid_arguments`, `dbus_error`, `not_found`,
`net.connman.Agent.Error.Canceled`.

---

## EventChannel API  `org.automotivelinux.connman/events`

All events include a `"timestamp"` field (`int64` epoch milliseconds).

| `type` field | Additional fields | Trigger |
|---|---|---|
| `managerStateChanged` | `state: String` | Manager PropertyChanged (State) |
| `servicesChanged` | `services: List<Map>` | ServicesChanged — full snapshot |
| `technologyPropertyChanged` | `path, name, value` | Technology PropertyChanged |
| `servicePropertyChanged` | `path, name, value` | Service PropertyChanged |

---

## Build

### Prerequisites

- AGL SDK or native build with ivi-homescreen + ivi-homescreen-plugins
- sdbus-c++ v2.x (bundled in `plugins/common/sdbus/third_party/sdbus-cpp`)
- glib-2.0
- asio (header-only, version ≥ 1.18)

### Enable the plugin

Add to your `cmake` configure invocation or `configs/your-config.json`:

```cmake
-DBUILD_PLUGIN_CONNMAN_SDBUS=ON
```

or in `plugins/CMakeLists.txt`:

```cmake
PLUGIN_OPTION(connman_sdbus "Include ConnMan sdbus plugin" OFF)
```

### Build

```bash
cd ~/bin/workspace-automation
python3 flutter_workspace.py --config configs/<your-config>.json

# Plugin-only rebuild:
cd build/
ninja plugin_connman_sdbus
```

### Run tests

```bash
cmake -DBUILD_UNIT_TESTS_CONNMAN=ON ...
ninja test_connman
ctest -R test_connman -V
```

### Regenerate D-Bus proxy headers

```bash
cd app/ivi-homescreen-plugins/plugins/connman_sdbus
./generate.sh
```

---

## Design Notes

### XML-driven proxies

ConnMan interface XML lives in `generated/xml/`.  Running `./generate.sh`
calls `sdbus-c++-xml2cpp` to regenerate the headers in `generated/`.
**Never edit generated headers by hand.**

### Dynamic service subscriptions

`ConnmanManager::DiffAndUpdateServiceProxies()` is called on every
`ServicesChanged` signal.  It:
1. Removes `WifiServiceHandler` instances for paths in `removed`.
2. Creates new handlers for paths absent from `service_proxies_`.
3. Merges property updates into existing handlers.

Proxy destruction happens **outside** any `SharedSink` lock to prevent
deadlock with in-flight `PropertyChanged` callbacks.

### Debounced PushUpdate

Rapid `PropertyChanged` bursts are coalesced by a 150 ms `g_timeout_add`
debounce.  The callback calls `GetServices()` and pushes a full snapshot
`servicesChanged` event to Dart.  All GLib source management is marshalled
to the GLib main loop via `RunOnMainThread`.

### ConnmanAgent RequestInput flow

```
ConnMan → RequestInput(service, fields)
         ↓
ConnmanAgent stores sdbus::Result + starts 60-s timeout
         ↓  InvokeMethod("requestInput", {requestId, fields})
Dart shows credential UI
         ↓  agentResponse({requestId, filledFields})
ConnmanPlugin.HandleResponse(requestId, fields)
         ↓  result.returnResults(variant_map)
ConnMan receives credentials
```

Timeout path: `g_timeout_add_seconds(60, …)` → `result.returnError(Canceled)`.
`Cancel()` drains **all** pending requests, not just the most recent.

---

## Phase 2+ Roadmap

| Priority | Feature |
|---|---|
| P0 | Ethernet support (`EthernetTechnologyHandler`) |
| P0 | IPv4/IPv6 configuration surface in `setServiceProperty` |
| P1 | Bluetooth tethering support |
| P1 | VPN provider management |
| P2 | P2P / Wi-Fi Direct peer management |
| P2 | Traffic counter EventChannel events |
| P3 | `getConnectedService` convenience method |

Adding a new technology type requires only a new `ConnmanTechnologyHandler`
subclass — zero changes to `ConnmanManager` or `ConnmanPlugin`.

---

## AGL Target Notes

- Tested on AGL Kukui (armv7, aarch64) with ConnMan 1.41+.
- The D-Bus session is the **system bus** (`net.connman` is a system service).
- `plugin_common_sdbus::SystemDBus::Instance()` manages the shared system bus
  connection with `enterEventLoopAsync()`.
- `org.automotivelinux.connman` must be allowed in the AGL Wayland compositor
  security policy for the homescreen application.
