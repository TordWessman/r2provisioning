#ifndef R2_PROVISIONING_HPP
#define R2_PROVISIONING_HPP

// r2provisioning — device provisioning for ESP32/ESP8266.
//
// On boot, `begin()` looks for a provisioning record in EEPROM:
//  - Found:     join the stored WiFi (retrying forever) and expose the stored
//               custom data.
//  - Not found: start a WiFi hotspot with a tiny HTTP server:
//                 GET  /ping       -> 200 {"name":"r2provisioning",...,"state":..}
//                 POST /provision  -> binary body (see ProvisioningPayload.hpp)
//                                     -> 202; the device then tries to join the
//                                     WiFi while keeping the hotspot up.
//    Provisioning succeeds only once the join works. Then the record is
//    stored and the device restarts into station mode. If the join fails,
//    nothing is stored, the hotspot stays up, and /ping reports the error.
//    Only a successfully provisioned network is retried forever.
//
// Firmware should use this class for WiFi setup instead of r2common's
// WiFiConnection directly.

#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <stddef.h>
#include <stdint.h>

#include "ProvisioningPayload.hpp"

namespace r2common {
class WiFiConnection;
}

namespace r2provisioning {

/// Library version string.
const char *version();

struct R2ProvisioningConfig {
    /// Hotspot SSID. nullptr = "r2-<last 3 MAC bytes as hex>".
    const char *apSsid = nullptr;
    /// Hotspot WPA2 password (8..63 chars). nullptr = open hotspot.
    const char *apPassword = nullptr;
    /// Port of the provisioning HTTP server.
    uint16_t httpPort = 80;
    /// Where the provisioning record starts in EEPROM. The record occupies
    /// `recordSize(maxCustomDataSize)` bytes from here; the rest is free for firmware.
    size_t eepromOffset = 0;
    /// Maximum accepted custom data length in bytes.
    uint16_t maxCustomDataSize = 512;
    /// How long to try joining a newly provisioned network before reporting failure.
    uint32_t joinTimeoutMs = 20000;
};

/// Progress of a provisioning attempt in hotspot mode (reported by GET /ping).
enum class JoinState : uint8_t {
    Idle,    ///< Waiting for POST /provision.
    Joining, ///< Trying the received credentials.
    Joined,  ///< Joined and stored; the device restarts shortly.
    Failed   ///< Last attempt failed; see joinError(). A new POST may be sent.
};

class R2Provisioning {
public:
    explicit R2Provisioning(const R2ProvisioningConfig &config = R2ProvisioningConfig());
    ~R2Provisioning();

    R2Provisioning(const R2Provisioning &) = delete;
    R2Provisioning &operator=(const R2Provisioning &) = delete;

    /// Load the stored record and start station or hotspot mode.
    /// Returns false on invalid configuration (EEPROM range, AP password).
    bool begin();

    /// Drive WiFi reconnection / the HTTP server / a pending restart. Call from `loop()`.
    void loop();

    /// True when a valid provisioning record is stored in EEPROM.
    bool isProvisioned() const { return provisioned_; }

    /// True when joined to the provisioned WiFi and holding an IP.
    bool isConnected() const;

    /// True while the provisioning hotspot is up.
    bool isHotspotActive() const { return hotspot_; }

    /// Progress of the current provisioning attempt (hotspot mode only).
    JoinState joinState() const { return joinState_; }

    /// Why the last attempt failed: "ssid_not_found", "connect_failed",
    /// "timeout" or "storage_failed". nullptr unless joinState() == Failed.
    const char *joinError() const { return joinError_; }

    /// Stored custom data (RAM copy, loaded by `begin()`). nullptr when empty.
    const uint8_t *customData() const { return customData_; }
    uint16_t customDataLength() const { return customDataLength_; }

    /// EEPROM address of the custom data's u16 little-endian length header.
    /// The bytes follow immediately after it.
    size_t customDataLengthEepromOffset() const { return config_.eepromOffset + kRecordDataLengthOffset; }

    /// EEPROM address of the first custom data byte.
    size_t customDataEepromOffset() const { return config_.eepromOffset + kRecordDataOffset; }

    /// Provisioned SSID (empty when not provisioned).
    const char *ssid() const { return ssid_; }

    /// Station IP when connected, hotspot IP while the hotspot is active.
    IPAddress localIP() const;

    /// Erase the provisioning record and restart into hotspot mode.
    void reset();

private:
    bool loadRecord();
    bool storeRecord(const ProvisioningData &data);
    void startStation();
    bool startHotspot();
    void updateJoin();
    void failJoin(const char *reason);
    void clearPending();
    void handleHttpClient(WiFiClient &client);
    void handleProvision(WiFiClient &client, long contentLength);
    void sendResponse(WiFiClient &client, int code, const char *status, const char *contentType, const char *body);

    R2ProvisioningConfig config_;
    bool provisioned_ = false;
    bool hotspot_ = false;
    bool eepromReady_ = false;

    char ssid_[kMaxSsidLength + 1] = {0};
    char password_[kMaxPasswordLength + 1] = {0};
    uint8_t *customData_ = nullptr;
    uint16_t customDataLength_ = 0;

    char deviceId_[7] = {0};
    char apSsid_[kMaxSsidLength + 1] = {0};

    r2common::WiFiConnection *wifi_ = nullptr;
    WiFiServer *server_ = nullptr;
    uint32_t restartRequestedAt_ = 0;
    bool restartPending_ = false;

    // Provisioning attempt in progress (hotspot mode).
    JoinState joinState_ = JoinState::Idle;
    const char *joinError_ = nullptr;
    uint8_t *pendingBody_ = nullptr; // owns the bytes pending_.data points into
    ProvisioningData pending_ = {};
    uint32_t joinStartedAt_ = 0;
    uint32_t joinedAt_ = 0;
    int lastJoinFailure_ = -1; // last failure-like WiFi.status() seen while joining
};

} // namespace r2provisioning

#endif // R2_PROVISIONING_HPP
