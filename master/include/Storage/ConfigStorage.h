#pragma once

#include <Arduino.h>
#include "Domain/SystemConfig.h"

// Loads and saves SystemConfig to LittleFS as JSON.
class ConfigStorage {
public:
    explicit ConfigStorage(String filePath);

    SystemConfig load() const;

    bool save(const SystemConfig& config) const;

    bool clear() const;

private:
    String filePath;
};
