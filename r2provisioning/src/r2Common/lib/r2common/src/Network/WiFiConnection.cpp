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

#include "WiFiConnection.h"

namespace r2common
{
    WiFiConnection::WiFiConnection(const char *ssid, const char *password, uint16_t port) :
        ssid(ssid),
        password(password),
        port(port),
        status(WiFiConnectionStatus::Disconnected),
        autoReconnect(true),
        reconnectIntervalMs(kDefaultReconnectIntervalMs),
        lastReconnectAttemptMs(0)
    {
    }

    void WiFiConnection::Begin()
    {
        WiFi.begin(ssid, password);
        status = WiFiConnectionStatus::Connecting;
    }

    bool WiFiConnection::Connect(uint32_t timeoutMs)
    {
        Begin();

        uint32_t elapsed = 0;
        while (WiFi.status() != WL_CONNECTED)
        {
            if (timeoutMs != kWaitForever && elapsed >= timeoutMs)
            {
                status = WiFiConnectionStatus::Failed;
                Serial.println();
                return false;
            }

            delay(kRetryDelayMs);
            elapsed += kRetryDelayMs;
            Serial.print(".");
        }

        status = WiFiConnectionStatus::Connected;
        Serial.println();
        return true;
    }

    WiFiConnectionEvent WiFiConnection::Loop()
    {
        if (IsConnected())
        {
            if (status != WiFiConnectionStatus::Connected)
            {
                // Link just came up (initial association or recovery after a drop).
                status = WiFiConnectionStatus::Connected;
                return WiFiConnectionEvent::Reconnected;
            }
            return WiFiConnectionEvent::None;
        }

        WiFiConnectionEvent event = WiFiConnectionEvent::None;

        if (status == WiFiConnectionStatus::Connected)
        {
            // We were connected but the link has since dropped. Report it once.
            status = WiFiConnectionStatus::Disconnected;
            event = WiFiConnectionEvent::Disconnected;
        }

        if (autoReconnect &&
            (lastReconnectAttemptMs == 0 || millis() - lastReconnectAttemptMs >= reconnectIntervalMs))
        {
            lastReconnectAttemptMs = millis();
            Begin(); // Re-issue WiFi.begin(ssid, password); sets status to Connecting.
            if (event == WiFiConnectionEvent::None)
            {
                event = WiFiConnectionEvent::Connecting;
            }
        }

        return event;
    }

    void WiFiConnection::SetAutoReconnect(bool enabled)
    {
        autoReconnect = enabled;
    }

    void WiFiConnection::SetReconnectInterval(uint32_t intervalMs)
    {
        reconnectIntervalMs = intervalMs;
    }

    bool WiFiConnection::IsConnected() const
    {
        return WiFi.status() == WL_CONNECTED;
    }

    WiFiConnectionStatus WiFiConnection::Status() const
    {
        if (IsConnected())
        {
            status = WiFiConnectionStatus::Connected;
        }
        else if (status == WiFiConnectionStatus::Connected)
        {
            // We were connected but the link has since dropped.
            status = WiFiConnectionStatus::Disconnected;
        }
        return status;
    }

    IPAddress WiFiConnection::GetIp() const
    {
        return WiFi.localIP();
    }

    String WiFiConnection::GetIpString() const
    {
        return WiFi.localIP().toString();
    }

    void WiFiConnection::CopyIp(uint8_t out[4]) const
    {
        IPAddress ip = WiFi.localIP();
        for (uint8_t i = 0; i < 4; i++)
        {
            out[i] = ip[i];
        }
    }

    uint16_t WiFiConnection::GetPort() const
    {
        return port;
    }

    void WiFiConnection::SetPort(uint16_t port)
    {
        this->port = port;
    }
}

#endif // ARDUINO
