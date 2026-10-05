#include <Arduino.h>
#include "App/AppController.h"

namespace {
    AppController appController("/config.json");  // o construtor so guarda o caminho
    bool ready = false;
}

void setup() {
    Serial.begin(115200);
    ready = appController.setup();
    if (!ready) {
        Serial.println("AppController: falha ao montar o LittleFS");
    }
}

void loop() {
    if (ready) {
        appController.execute();
    }
    delay(10);
}
