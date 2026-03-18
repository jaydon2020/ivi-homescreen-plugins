# connman_sdbus plugin

## File roles (don't mix concerns)
- `connman_plugin.cc`     — registration + channel wiring only
- `connman_manager.cc`    — net.connman.Manager proxy + ServicesChanged
- `connman_agent.cc`      — credential dialog bridge, ID-keyed pending map
- `wifi/*`                — all wifi-specific logic lives here
- `helpers.h`             — SharedSink, GetArgument<T>, RunOnMainThread
- `messages.g.cc`         — MethodChannel dispatch table, nothing else
- `generated/`            — DO NOT EDIT, output of sdbus-c++-xml2cpp

## Regenerate proxies
```bash
cd app/ivi-homescreen-plugins/plugins/connman_sdbus
./generate.sh
```

## Run clang-format
```bash
clang-format -i --style=file $(find . -name "*.cc" -o -name "*.h" | grep -v generated)
```

## Known cross-references
- SharedSink defined in helpers.h — include it explicitly, don't rely on order
- strength (uint8_t from D-Bus) must be cast to int32_t — grep "GetByteProp" for all sites