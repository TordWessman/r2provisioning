// Sandbox sketch for developing r2provisioning in-tree.
#include <Arduino.h>

#include "r2provisioning/R2Provisioning.hpp"

r2provisioning::R2Provisioning provisioning;

void setup() {
    Serial.begin(115200);
    provisioning.begin();
}

void loop() {
    provisioning.loop();
    delay(10);
}
