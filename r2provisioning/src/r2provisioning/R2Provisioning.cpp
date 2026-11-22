#include "R2Provisioning.hpp"

#include <EEPROM.h>
#include <string.h>

#include "Network/WiFiConnection.h"
#include "R2Logger.h"

namespace r2provisioning {

namespace {

// ESP8266 and ESP32 both emulate EEPROM in a single flash sector.
const size_t kEepromMaxSize = 4096;
const uint32_t kRestartDelayMs = 1000;
// After a successful join, stay up this long for the client to read the result
// via /ping. The device restarts sooner, once a ping has reported it.
const uint32_t kJoinedLingerMs = 30000;
const uint32_t kHttpTimeoutMs = 3000;

using r2common::R2Logger;

const char *joinStateName(JoinState state) {
    switch (state) {
    case JoinState::Joining: return "joining";
    case JoinState::Joined: return "joined";
    case JoinState::Failed: return "failed";
    default: return "idle";
    }
}

} // namespace

const char *version() { return "0.0.1"; }

R2Provisioning::R2Provisioning(const R2ProvisioningConfig &config) : config_(config) {}

R2Provisioning::~R2Provisioning() {
    delete wifi_;
    delete server_;
    delete[] customData_;
    delete[] pendingBody_;
}

bool R2Provisioning::begin() {
    R2Logger::m("r2provisioning: begin v", version());

    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(deviceId_, sizeof(deviceId_), "%02X%02X%02X", mac[3], mac[4], mac[5]);

    size_t eepromSize = config_.eepromOffset + recordSize(config_.maxCustomDataSize);
    if (eepromSize > kEepromMaxSize) {
        R2Logger::e("r2provisioning: record does not fit in EEPROM; bytes needed: ", (uint32_t)eepromSize);
        return false;
    }
    EEPROM.begin(eepromSize);
    eepromReady_ = true;

    // Credentials are owned by r2provisioning; keep the SDK from persisting its own copy.
    WiFi.persistent(false);

    if (loadRecord()) {
        startStation();
        return true;
    }
    return startHotspot();
}

void R2Provisioning::loop() {
    if (restartPending_ && millis() - restartRequestedAt_ >= kRestartDelayMs) {
        R2Logger::m("r2provisioning: restarting");
        delay(50);
        ESP.restart();
    }

    if (joinState_ == JoinState::Joining) {
        updateJoin();
    } else if (joinState_ == JoinState::Joined && !restartPending_ && millis() - joinedAt_ >= kJoinedLingerMs) {
        restartPending_ = true;
        restartRequestedAt_ = millis();
    }

    if (wifi_ != nullptr) {
        switch (wifi_->Loop()) {
        case r2common::WiFiConnectionEvent::Reconnected:
            R2Logger::m("r2provisioning: connected, IP ", WiFi.localIP().toString().c_str());
            break;
        case r2common::WiFiConnectionEvent::Disconnected:
            R2Logger::m("r2provisioning: WiFi link lost, reconnecting to ", ssid_);
            break;
        default:
            break;
        }
    }

    if (server_ != nullptr) {
#ifdef ESP8266
        WiFiClient client = server_->accept();
#else
        WiFiClient client = server_->available();
#endif
        if (client) {
            handleHttpClient(client);
        }
    }
}

bool R2Provisioning::isConnected() const {
    return wifi_ != nullptr && wifi_->IsConnected();
}

IPAddress R2Provisioning::localIP() const {
    return hotspot_ ? WiFi.softAPIP() : WiFi.localIP();
}

void R2Provisioning::reset() {
    if (!eepromReady_) {
        EEPROM.begin(config_.eepromOffset + recordSize(config_.maxCustomDataSize));
        eepromReady_ = true;
    }
    for (size_t i = 0; i < 4; i++) {
        EEPROM.write(config_.eepromOffset + i, 0);
    }
    EEPROM.commit();
    R2Logger::m("r2provisioning: record erased, restarting");
    delay(50);
    ESP.restart();
}

// --- EEPROM --------------------------------------------------------------------

bool R2Provisioning::loadRecord() {
    size_t size = recordSize(config_.maxCustomDataSize);
    uint8_t *rec = new uint8_t[size];
    for (size_t i = 0; i < size; i++) {
        rec[i] = EEPROM.read(config_.eepromOffset + i);
    }

    ProvisioningData data;
    bool ok = decodeRecord(rec, config_.maxCustomDataSize, data);
    if (ok) {
        strncpy(ssid_, data.ssid, sizeof(ssid_));
        strncpy(password_, data.password, sizeof(password_));
        delete[] customData_;
        customData_ = nullptr;
        customDataLength_ = data.dataLength;
        if (customDataLength_ > 0) {
            customData_ = new uint8_t[customDataLength_];
            memcpy(customData_, data.data, customDataLength_);
        }
        R2Logger::m("r2provisioning: provisioned for SSID ", ssid_);
        R2Logger::m("r2provisioning: custom data bytes: ", (uint32_t)customDataLength_);
    } else {
        R2Logger::m("r2provisioning: no valid provisioning record");
    }

    delete[] rec;
    provisioned_ = ok;
    return ok;
}

bool R2Provisioning::storeRecord(const ProvisioningData &data) {
    size_t size = recordSize(config_.maxCustomDataSize);
    uint8_t *rec = new uint8_t[size];
    bool ok = encodeRecord(data, config_.maxCustomDataSize, rec);
    if (ok) {
        for (size_t i = 0; i < size; i++) {
            EEPROM.write(config_.eepromOffset + i, rec[i]);
        }
        ok = EEPROM.commit();
    }
    delete[] rec;
    return ok;
}

// --- WiFi modes ----------------------------------------------------------------

void R2Provisioning::startStation() {
    hotspot_ = false;
    WiFi.mode(WIFI_STA);
    wifi_ = new r2common::WiFiConnection(ssid_, password_);
    wifi_->SetAutoReconnect(true); // retry forever; never fall back to the hotspot
    wifi_->Begin();
    R2Logger::m("r2provisioning: connecting to ", ssid_);
}

bool R2Provisioning::startHotspot() {
    const char *apSsid = config_.apSsid;
    if (apSsid == nullptr) {
        snprintf(apSsid_, sizeof(apSsid_), "r2-%s", deviceId_);
        apSsid = apSsid_;
    }

    const char *apPassword = config_.apPassword;
    if (apPassword != nullptr) {
        size_t len = strlen(apPassword);
        if (len < 8 || len > 63) {
            R2Logger::e("r2provisioning: AP password must be 8..63 characters");
            return false;
        }
    }

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(apSsid, apPassword)) {
        R2Logger::e("r2provisioning: failed to start hotspot ", apSsid);
        return false;
    }
    hotspot_ = true;

    server_ = new WiFiServer(config_.httpPort);
    server_->begin();

    R2Logger::m("r2provisioning: hotspot started: ", apSsid);
    R2Logger::m("r2provisioning: listening on ", WiFi.softAPIP().toString().c_str());
    return true;
}

// --- Join verification (hotspot mode) -----------------------------------------
// The received credentials are tried in AP+STA mode while the hotspot stays up.
// Joining moves the hotspot to the router's channel, which can drop the phone
// for a moment, so the outcome is reported through /ping, not the POST reply.

void R2Provisioning::updateJoin() {
    int status = WiFi.status();
    if (status == WL_CONNECTED) {
        if (!storeRecord(pending_)) {
            failJoin("storage_failed");
            return;
        }
        R2Logger::m("r2provisioning: joined and stored, SSID ", pending_.ssid);
        joinState_ = JoinState::Joined;
        joinedAt_ = millis();
        clearPending();
        return;
    }

    if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED
#ifdef ESP8266
        || status == WL_WRONG_PASSWORD
#endif
    ) {
        lastJoinFailure_ = status;
    }

    if (millis() - joinStartedAt_ >= config_.joinTimeoutMs) {
        if (lastJoinFailure_ == WL_NO_SSID_AVAIL) {
            failJoin("ssid_not_found");
        } else if (lastJoinFailure_ >= 0) {
            failJoin("connect_failed");
        } else {
            failJoin("timeout");
        }
    }
}

void R2Provisioning::failJoin(const char *reason) {
    R2Logger::e("r2provisioning: provisioning failed: ", reason);
    WiFi.disconnect(false);
    WiFi.mode(WIFI_AP);
    joinState_ = JoinState::Failed;
    joinError_ = reason;
    clearPending();
}

void R2Provisioning::clearPending() {
    delete[] pendingBody_;
    pendingBody_ = nullptr;
    memset(&pending_, 0, sizeof(pending_));
}

// --- HTTP ----------------------------------------------------------------------
// Minimal HTTP/1.1 server (one request per connection). A hand-rolled server is
// used instead of WebServer because the ESP32 core's WebServer exposes request
// bodies as NUL-terminated Strings, which truncates binary payloads.

void R2Provisioning::handleHttpClient(WiFiClient &client) {
    client.setTimeout(kHttpTimeoutMs);

    String requestLine = client.readStringUntil('\n');
    requestLine.trim();
    int sp1 = requestLine.indexOf(' ');
    int sp2 = requestLine.indexOf(' ', sp1 + 1);
    if (sp1 <= 0 || sp2 <= sp1) {
        sendResponse(client, 400, "Bad Request", "text/plain", "bad request");
        return;
    }
    String method = requestLine.substring(0, sp1);
    String path = requestLine.substring(sp1 + 1, sp2);
    int query = path.indexOf('?');
    if (query >= 0) {
        path = path.substring(0, query);
    }

    long contentLength = -1;
    bool expectContinue = false;
    while (client.connected() || client.available()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
            break;
        }
        header.toLowerCase();
        if (header.startsWith("content-length:")) {
            contentLength = header.substring(15).toInt();
        } else if (header.startsWith("expect:") && header.indexOf("100-continue") > 0) {
            expectContinue = true;
        }
    }

    if (method == "GET" && path == "/ping") {
        char error[48] = "";
        if (joinState_ == JoinState::Failed && joinError_ != nullptr) {
            snprintf(error, sizeof(error), ",\"error\":\"%s\"", joinError_);
        }
        char body[224];
        snprintf(body, sizeof(body),
                 "{\"name\":\"r2provisioning\",\"version\":\"%s\",\"id\":\"%s\",\"maxDataSize\":%u,"
                 "\"state\":\"%s\"%s}",
                 version(), deviceId_, (unsigned)config_.maxCustomDataSize, joinStateName(joinState_), error);
        sendResponse(client, 200, "OK", "application/json", body);
        if (joinState_ == JoinState::Joined && !restartPending_) {
            // The client has now seen the result.
            restartPending_ = true;
            restartRequestedAt_ = millis();
        }
    } else if (method == "POST" && path == "/provision") {
        if (expectContinue) {
            client.print("HTTP/1.1 100 Continue\r\n\r\n");
        }
        handleProvision(client, contentLength);
    } else {
        sendResponse(client, 404, "Not Found", "text/plain", "not found");
    }
}

void R2Provisioning::handleProvision(WiFiClient &client, long contentLength) {
    if (joinState_ == JoinState::Joining || joinState_ == JoinState::Joined) {
        sendResponse(client, 409, "Conflict", "text/plain", "provisioning in progress");
        return;
    }
    if (contentLength <= 0) {
        sendResponse(client, 400, "Bad Request", "text/plain", "missing body");
        return;
    }

    size_t maxSize = maxPayloadSize(config_.maxCustomDataSize);
    if ((size_t)contentLength > maxSize) {
        // Drain (bounded) so the client sees the response rather than a reset.
        uint8_t scratch[64];
        long remaining = contentLength;
        uint32_t start = millis();
        while (remaining > 0 && remaining <= 65536 && millis() - start < kHttpTimeoutMs &&
               (client.connected() || client.available())) {
            size_t n = client.readBytes(scratch, remaining < (long)sizeof(scratch) ? remaining : sizeof(scratch));
            if (n == 0) { break; }
            remaining -= n;
        }
        sendResponse(client, 413, "Payload Too Large", "text/plain", "payload too large");
        return;
    }

    uint8_t *body = new uint8_t[contentLength];
    size_t got = client.readBytes(body, contentLength);
    if (got != (size_t)contentLength) {
        delete[] body;
        sendResponse(client, 400, "Bad Request", "text/plain", "incomplete body");
        return;
    }

    ParseResult result = parsePayload(body, got, config_.maxCustomDataSize, pending_);
    if (result != ParseResult::Ok) {
        delete[] body;
        clearPending();
        if (result == ParseResult::TooLarge) {
            sendResponse(client, 413, "Payload Too Large", "text/plain", "custom data too large");
        } else {
            sendResponse(client, 400, "Bad Request", "text/plain", "malformed payload");
        }
        return;
    }

    // Keep the body alive: pending_.data points into it until the join resolves.
    pendingBody_ = body;
    joinState_ = JoinState::Joining;
    joinError_ = nullptr;
    joinStartedAt_ = millis();
    lastJoinFailure_ = -1;
    sendResponse(client, 202, "Accepted", "text/plain", "joining");

    R2Logger::m("r2provisioning: trying to join ", pending_.ssid);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(pending_.ssid, pending_.password);
}

void R2Provisioning::sendResponse(WiFiClient &client, int code, const char *status, const char *contentType,
                                  const char *body) {
    char header[160];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", code, status,
             contentType, (unsigned)strlen(body));
    client.print(header);
    client.print(body);
    client.flush();
    delay(1);
    client.stop();
}

} // namespace r2provisioning
