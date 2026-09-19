#pragma once

#include <memory>

#include "Domain/SystemConfig.h"
#include "Storage/CallQueueStorage.h"
#include "Storage/ConfigStorage.h"

class AppController {
public:
    AppController();

    void execute();

private:
    SystemConfig config;
    ConfigStorage configStorage;

    // Construir somente depois que o LittleFS estiver montado.
    std::unique_ptr<CallQueueStorage> callQueueStorage = nullptr;
};
