#pragma once

#include <Arduino.h>
#include "Domain/MasterLink.h"

// Persists the learned MasterLink as a single JSON file on LittleFS,
// mirroring master/'s ConfigStorage shape exactly (load/save/clear).
class MasterLinkStorage {
public:
    explicit MasterLinkStorage(String filePath);

    MasterLink load() const;
    bool save(const MasterLink& link) const;
    bool clear() const;

private:
    String filePath;
};
