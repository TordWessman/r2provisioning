# r2provisioning

Device provisioning framework, split into a firmware library and an Apple client SDK. **Scaffold only** — no provisioning logic yet.

## Projects

### r2provisioning (`r2provisioning/`)
C++ firmware library for ESP32/ESP8266 (PlatformIO, Arduino framework). Depends on the shared `r2common` library, which is **not vendored**: it is linked from the sibling checkout `../r2common/lib/r2common` via PlatformIO `symlink://` entries in `lib_deps` (root `platformio.ini` and every example's `platformio.ini`). Include r2common headers as `"R2Logger.h"`, `"Result.h"`, `"Network/WiFiConnection.h"`, all in namespace `r2common`.

### r2provisioning-ios (`client-sdks/r2provisioning-ios/`)
Swift Package (macOS 26+, iOS 15+) client SDK for provisioning r2 devices from Apple platforms. Library product `r2provisioning`.

## Commands

- Firmware: `cd r2provisioning && pio run -e esp32dev` (or `-e esp8266`)
- Example: `cd r2provisioning/examples/basic && pio run`
- Swift: `cd client-sdks/r2provisioning-ios && swift build && swift test`
