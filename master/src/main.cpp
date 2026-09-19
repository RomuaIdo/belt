#include <Arduino.h>
#include "App/AppController.h"

static AppController appController;

void setup() {}

void loop() {
    appController.execute();
}
