#include "Storage/MasterLinkStorage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

MasterLinkStorage::MasterLinkStorage(String filePath) : filePath(std::move(filePath)) {}

MasterLink MasterLinkStorage::load() const {
    if (!LittleFS.exists(filePath)) {
        return MasterLink();
    }

    File file = LittleFS.open(filePath, "r");
    if (!file) {
        return MasterLink();
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.printf("MasterLinkStorage: failed to parse %s: %s\n", filePath.c_str(), error.c_str());
        return MasterLink();
    }

    return MasterLink(doc["masterMac"] | "");
}

bool MasterLinkStorage::save(const MasterLink& link) const {
    JsonDocument doc;
    doc["masterMac"] = link.getMacAddress();

    File file = LittleFS.open(filePath, "w");
    if (!file) {
        Serial.printf("MasterLinkStorage: failed to open %s for writing\n", filePath.c_str());
        return false;
    }

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

bool MasterLinkStorage::clear() const {
    if (!LittleFS.exists(filePath)) return true;
    return LittleFS.remove(filePath);
}
