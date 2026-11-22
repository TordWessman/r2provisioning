// Sandbox sketch for developing r2provisioning in-tree.
//
// Unprovisioned: starts hotspot "r2-XXXXXX" (open) with GET /ping and
// POST /provision on http://192.168.4.1. Provisioned: joins the stored WiFi
// and prints the stored custom data once connected.
#include <Arduino.h>

#include "r2provisioning/R2Provisioning.hpp"

r2provisioning::R2Provisioning provisioning;

static void printCustomData() {
    Serial.printf("custom data (%u bytes):", (unsigned)provisioning.customDataLength());
    for (uint16_t i = 0; i < provisioning.customDataLength(); i++) {
        Serial.printf(" %02X", provisioning.customData()[i]);
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    provisioning.begin();
}

void loop() {
    static bool wasConnected = false;
    provisioning.loop();

    bool connected = provisioning.isConnected();
    if (connected && !wasConnected) {
        Serial.print("connected to ");
        Serial.print(provisioning.ssid());
        Serial.print(", IP ");
        Serial.println(provisioning.localIP());
        printCustomData();
    }
    wasConnected = connected;

    // Type 'r' on the serial monitor to erase provisioning and restart.
    if (Serial.available() && Serial.read() == 'r') {
        provisioning.reset();
    }
    delay(10);
}
