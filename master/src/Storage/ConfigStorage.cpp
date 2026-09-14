#include "Storage/ConfigStorage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

ConfigStorage::ConfigStorage(String filePath) : filePath(std::move(filePath)) {}

SystemConfig ConfigStorage::load() const {
    SystemConfig config; //fallback

    if (!LittleFS.exists(filePath)) {
        return config;
    }

    File file = LittleFS.open(filePath, "r");
    if (!file) {
        return config;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.printf("ConfigStorage: failed to parse %s: %s\n", filePath.c_str(), error.c_str());
        return config;
    }

    config.wifiSsid = doc["wifiSsid"] | "";
    config.wifiPassword = doc["wifiPassword"] | "";
    config.telegramBotToken = doc["telegramBotToken"] | "";

    JsonArrayConst peers = doc["peers"].as<JsonArrayConst>();
    for (JsonObjectConst peerJson : peers) {
        PeerNode peer(peerJson["mac"] | "", peerJson["alias"] | "");
        JsonArrayConst phones = peerJson["phones"].as<JsonArrayConst>();
        for (const char* phone : phones) {
            if (phone) peer.addPhone(phone);
        }
        config.addPeer(peer);
    }

    return config;
}

bool ConfigStorage::save(const SystemConfig& config) const {
    JsonDocument doc;
    doc["wifiSsid"] = config.wifiSsid;
    doc["wifiPassword"] = config.wifiPassword;
    doc["telegramBotToken"] = config.telegramBotToken;

    JsonArray peers = doc["peers"].to<JsonArray>();
    for (const auto& peer : config.getPeers()) {
        JsonObject peerJson = peers.add<JsonObject>();
        peerJson["mac"] = peer.getMacAddress();
        peerJson["alias"] = peer.getAlias();
        JsonArray phones = peerJson["phones"].to<JsonArray>();
        for (const auto& phone : peer.getPhones()) {
            phones.add(phone);
        }
    }

    File file = LittleFS.open(filePath, "w");
    if (!file) {
        Serial.printf("ConfigStorage: failed to open %s for writing\n", filePath.c_str());
        return false;
    }

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

bool ConfigStorage::clear() const {
    if (!LittleFS.exists(filePath)) return true;
    return LittleFS.remove(filePath);
}
