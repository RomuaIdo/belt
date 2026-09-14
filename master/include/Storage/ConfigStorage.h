#pragma once

#include <Arduino.h>
#include "Domain/SystemConfig.h"

// Uses LittleFS to maintain a JSON file with all the configs in the flash.
// Its an interface to load and save the SystemConfig object to the flash memory.
class ConfigStorage {
public:
    explicit ConfigStorage(String filePath);

    SystemConfig load() const;

    bool save(const SystemConfig& config) const;

    bool clear() const;

private:
    String filePath;
};
