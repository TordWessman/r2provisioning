# r2provisioning

Device provisioning framework, split into a firmware library and an Apple client SDK. A device that isn't configured starts a WiFi hotspot and receives WiFi credentials plus opaque "custom data" over HTTP. It then tries to join that WiFi while keeping the hotspot up (AP+STA). Provisioning succeeds only if the join works. Only then are the credentials stored in EEPROM and the device restarts. From then on the stored WiFi is retried forever, with no fallback to the hotspot. If the join fails, nothing is stored and the hotspot stays up for another attempt.

## Protocol (hotspot mode, `http://192.168.4.1`, port configurable)
- `GET /ping` → `200 {"name":"r2provisioning","version":..,"id":"<MAC6>","maxDataSize":512,"state":"idle|joining|joined|failed"[,"error":".."]}`
- `POST /provision`, binary LE body: `u8 ssidLen(1..32) | ssid | u8 pwLen(0..64) | pw | u16 dataLen | data`
  → `202` (join attempt started) / `400` malformed / `409` attempt running / `413` too large
- The join result is reported through `/ping` `state`, not in the POST reply. When the device joins the router, its hotspot moves to the router's channel and the phone can drop off briefly. `error` is one of `ssid_not_found`, `connect_failed`, `timeout` or `storage_failed`. After a ping has reported `joined`, the device restarts about 1 s later (after 30 s at most).
- The firmware uses a hand-rolled HTTP server on `WiFiServer`, not `WebServer`. The ESP32 2.x `WebServer` truncates binary bodies at NUL bytes.

## EEPROM record (from `eepromOffset`, default 0)
`u32 magic 'R2PV' | u8 version=1 | u8 ssidLen | ssid[32] | u8 pwLen | pw[64] | u16 dataLen | data[maxCustomDataSize] | u32 crc32`

## Projects

### r2provisioning (`r2provisioning/`)
C++ firmware library for ESP32/ESP8266 (PlatformIO, Arduino framework). Firmware uses `R2Provisioning` (`begin()`/`loop()`, `isConnected()`, `customData()`, `reset()`) for WiFi setup instead of r2common's `WiFiConnection`, which it wraps internally. `joinState()`/`joinError()` expose the attempt in hotspot mode, and `joinTimeoutMs` (default 20 s) is configurable. `ProvisioningPayload.{hpp,cpp}` is Arduino-free (wire and EEPROM codec plus CRC32) and is unit-tested natively.

Depends on the shared `r2common` library (vendored as a git subtree under `r2provisioning/src/r2Common/`, same layout as r2com). Include r2common headers as `"R2Logger.h"`, `"Result.h"`, `"Network/WiFiConnection.h"`, all in namespace `r2common`.

#### r2common is a git subtree — and lives in TWO places

⚠️ There are **two copies** of r2common, consumed by different builds. Edit **both** or they diverge:

1. **Subtree** `r2provisioning/src/r2Common/lib/r2common/` — compiled by the **top-level project** (`platformio.ini` uses `-Isrc/r2Common/lib/r2common/src` and filters out the subtree's own mains/tests).
2. **Standalone upstream repo** `../r2common/lib/r2common/` — consumed by everything under `r2provisioning/examples/` via `symlink://` in their `platformio.ini`.

Reconcile with the `r2common` git remote rather than leaving parallel edits (run from the repo root):

```sh
git subtree pull --prefix=r2provisioning/src/r2Common r2common main --squash
git subtree push --prefix=r2provisioning/src/r2Common r2common main
```

### r2provisioning-ios (`client-sdks/r2provisioning-ios/`)
Swift Package (macOS 26+, iOS 15+) client SDK. Library product `r2provisioning`. `R2ProvisioningClient` (`@MainActor`, `ObservableObject` where Combine is available) offers `start()`/`stop()` ping polling (`isConnected`, `deviceInfo`) and `send(ssid:password:data:)`. `send` posts the payload, then polls `/ping` until `joined` (sets `isProvisioned`) or `failed` (throws `.joinFailed(reason:)`). It tolerates failed pings during the wait and throws `.joinTimeout` after `joinTimeout` (default 45 s). `R2ProvisioningPayload.encode` is the wire encoder.

## Commands

- Firmware: `cd r2provisioning && pio run -e esp32dev` (or `-e esp8266`)
- Firmware unit tests (host): `cd r2provisioning && pio test -e native`
- Example: `cd r2provisioning/examples/basic && pio run`
- Swift: `cd client-sdks/r2provisioning-ios && swift build && swift test` (on Linux: `docker run --rm -v "$PWD":/pkg -w /pkg swift:6.2 swift test --scratch-path /tmp/build`)
