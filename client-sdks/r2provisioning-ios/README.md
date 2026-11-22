# r2provisioning (Swift)

Client SDK for provisioning r2 devices that are in hotspot mode.

```swift
let client = R2ProvisioningClient()          // http://192.168.4.1:80
client.start()                               // ping once per second → client.isConnected
// ... when client.isConnected:
do {
    try await client.send(ssid: "home", password: "secret", data: Data([0x01, 0x02]))
    // The device joined "home" and stored it. client.isProvisioned == true,
    // polling has stopped, and the device restarts.
} catch R2ProvisioningError.joinFailed(let reason) {
    // e.g. "connect_failed" (wrong password). Nothing was stored, and the device is
    // still a hotspot, so let the user correct the input and send again.
}
```

- `R2ProvisioningClient` is `@MainActor` and an `ObservableObject` on Apple platforms, so you can observe it from SwiftUI.
- Polling runs only between `start()` and `stop()`, and stops for good once `send` succeeds.
- `send` returns only after the device has actually joined the network. The device reports the result through `/ping`, and the phone may briefly lose the hotspot while the device joins, so failed pings during that wait are tolerated. Defaults: `joinTimeout` is 45 s on the client and `joinTimeoutMs` is 20 s on the firmware.
- `send` validates input before sending: the SSID must be 1–32 UTF-8 bytes and the password at most 64. The data must fit the device's `maxDataSize`, which is learned from `/ping`.

## App requirements
- The phone has to join the device hotspot (`r2-XXXXXX`) itself, either in Settings or with `NEHotspotConfiguration` (that needs the Hotspot Configuration entitlement).
- Info.plist entries:
  - `NSAppTransportSecurity` → `NSAllowsLocalNetworking = YES` (the device speaks plain HTTP)
  - `NSLocalNetworkUsageDescription`

```sh
swift build && swift test
```

## Example app
`Example/` contains a SwiftUI iOS app that uses this package as a local dependency. Open `Example/R2ProvisioningExample.xcodeproj`, or regenerate it from `Example/project.yml` with `cd Example && xcodegen generate`.
