# r2provisioning

Device provisioning framework, split into a firmware library and an Apple client SDK. **Scaffold only** — no provisioning logic yet.

## Projects

### r2provisioning (`r2provisioning/`)
C++ firmware library for ESP32/ESP8266 (PlatformIO, Arduino framework). Depends on the shared `r2common` library (vendored as a git subtree under `r2provisioning/src/r2Common/`, same layout as r2com). Include r2common headers as `"R2Logger.h"`, `"Result.h"`, `"Network/WiFiConnection.h"`, all in namespace `r2common`.

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
Swift Package (macOS 26+, iOS 15+) client SDK for provisioning r2 devices from Apple platforms. Library product `r2provisioning`.

## Commands

- Firmware: `cd r2provisioning && pio run -e esp32dev` (or `-e esp8266`)
- Example: `cd r2provisioning/examples/basic && pio run`
- Swift: `cd client-sdks/r2provisioning-ios && swift build && swift test`
