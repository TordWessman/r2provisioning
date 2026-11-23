/**
 * MIT License
 *
 * Copyright (c) 2023 Tord Wessman
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef ARDUINO

#ifndef __WIFI_CONNECTION_H__
#define __WIFI_CONNECTION_H__

#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <stdint.h>

namespace r2common
{
    /// @brief Connection state of a `WiFiConnection`.
    enum class WiFiConnectionStatus
    {
        Disconnected,   // No connection attempt has been started, or the link dropped.
        Connecting,     // `Begin()`/`Connect()` issued, waiting for association.
        Connected,      // Associated with the access point and holding an IP.
        Failed          // `Connect()` gave up after its timeout elapsed.
    };

    /// @brief Link transition reported by `Loop()`.
    enum class WiFiConnectionEvent
    {
        None,           // No change since the previous `Loop()` call.
        Connecting,     // A (re)connection attempt was (re)issued this call.
        Reconnected,    // Link restored: was down, now associated and holding an IP.
        Disconnected    // Link loss detected this call.
    };

    /// @brief Platform-agnostic wrapper around the ESP32/ESP8266 WiFi station API.
    ///
    /// Hides the `<WiFi.h>` / `<ESP8266WiFi.h>` split, owns the SSID/password/port
    /// endpoint data, and exposes IP + connection-status helpers so sketches no longer
    /// hand-roll the `WiFi.begin()` + `while (WiFi.status() != WL_CONNECTED)` boilerplate.
    class WiFiConnection
    {

    public:

        /// @brief `Connect()` timeout sentinel: wait indefinitely for association.
        static const uint32_t kWaitForever = 0;

        /// @brief Delay between connection-status polls while associating.
        static const uint32_t kRetryDelayMs = 500;

        /// @brief Default delay between reconnection attempts while the link is down.
        static const uint32_t kDefaultReconnectIntervalMs = 5000;

        /// @brief How long `Loop()` lets an in-progress join (`WL_DISCONNECTED`) run
        ///        before re-issuing `WiFi.begin()` anyway.
        static const uint32_t kJoinStallMs = 30000;

        /// @brief Create a connection descriptor. Does not touch the radio until
        ///        `Begin()` or `Connect()` is called.
        /// @param ssid WiFi network name.
        /// @param password WiFi password.
        /// @param port Endpoint/service port stored alongside the connection (e.g. the
        ///             TCP listen port). Pass 0 when unused.
        WiFiConnection(const char *ssid, const char *password, uint16_t port = 0);

        /// @brief Start association without blocking (issues `WiFi.begin()`).
        void Begin();

        /// @brief Start association and block until connected or the timeout elapses.
        ///        Prints a "." to Serial for every poll while waiting.
        /// @param timeoutMs Milliseconds to wait; `kWaitForever` (0) waits indefinitely.
        /// @return true once associated, false if the timeout elapsed first.
        bool Connect(uint32_t timeoutMs = kWaitForever);

        /// @brief Non-blocking connection watchdog. Call once per `loop()` iteration.
        ///        Detects link loss and, while auto-reconnect is enabled, re-issues
        ///        association at most once per reconnect interval. Never blocks.
        ///
        ///        Callers that dial out (e.g. through a proxy) should re-open their
        ///        socket when this returns `Reconnected`, since the transport dies
        ///        with the WiFi link.
        /// @return The link transition observed on this call (see WiFiConnectionEvent).
        WiFiConnectionEvent Loop();

        /// @brief Enable or disable automatic reconnection in `Loop()`. Enabled by default.
        void SetAutoReconnect(bool enabled);

        /// @brief Delay between reconnection attempts while the link is down.
        void SetReconnectInterval(uint32_t intervalMs);

        /// @brief true when the station is associated and holding an IP (`WL_CONNECTED`).
        bool IsConnected() const;

        /// @brief Current connection status.
        WiFiConnectionStatus Status() const;

        /// @brief Local IP once connected (0.0.0.0 otherwise).
        IPAddress GetIp() const;

        /// @brief Local IP as a dotted-quad string ("0.0.0.0" when not connected).
        String GetIpString() const;

        /// @brief Copy the four IPv4 octets into `out` (e.g. for UDP discovery
        ///        `setLocalIp`). Zero-filled when not connected.
        void CopyIp(uint8_t out[4]) const;

        /// @brief Stored endpoint/service port.
        uint16_t GetPort() const;

        /// @brief Update the stored endpoint/service port.
        void SetPort(uint16_t port);

    private:

        const char *ssid;
        const char *password;
        uint16_t port;
        mutable WiFiConnectionStatus status;
        bool autoReconnect;
        uint32_t reconnectIntervalMs;
        uint32_t lastReconnectAttemptMs;
    };
}

#endif // __WIFI_CONNECTION_H__

#endif // ARDUINO
