#include <Arduino.h>
#include "App/AppController.h"

namespace {
    AppController appController("/config.json");
    bool ready = false;
}

void setup() {
    Serial.begin(115200);
    ready = appController.setup();
    if (!ready) {
        Serial.println("AppController: failed to mount LittleFS");
    }
}

void loop() {
    if (ready) {
        appController.execute();
    }
    delay(10);
}
