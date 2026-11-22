#include <Arduino.h>

#include "R2Logger.h"
#include "r2provisioning/R2Provisioning.hpp"

r2provisioning::R2Provisioning provisioning;

void setup() {
    Serial.begin(115200);
    r2common::R2Logger::m("basic example starting");
    provisioning.begin();
}

void loop() {
    provisioning.loop();
    delay(10);
}
