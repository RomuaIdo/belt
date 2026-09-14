#include <Arduino.h>
#include "App/AppController.h"

static AppController appController;

void setup() {
    appController.setup();
}

void loop() {
    appController.loop();
}
