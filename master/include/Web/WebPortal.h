#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <functional>
#include <vector>

#include "Domain/PendingPairing.h"
#include "Domain/SystemConfig.h"
#include "Storage/ConfigStorage.h"

// Web portal serving LittleFS configuration frontend and JSON API.
class WebPortal {
public:
    using PendingPairingsProvider = std::function<std::vector<PendingPairing>()>;
    using DiscardPendingCallback = std::function<void(const String& mac)>;
    // Callback to persist belt (triggers mode 2 handshake for new MACs).
    using SaveDeviceCallback = std::function<bool(const PeerNode& peer)>;

    // Invoked after saving new Wi-Fi credentials to reconnect.
    WebPortal(SystemConfig& config, ConfigStorage& configStorage,
              std::function<void()> onWifiChanged);

    void begin();   // Registers routes and starts HTTP server on port 80
    void handle();  // Processes incoming client requests in loop()

    void setListPendingPairings(PendingPairingsProvider callback) { listPendingPairings = std::move(callback); }
    void setOnDiscardPending(DiscardPendingCallback callback) { onDiscardPending = std::move(callback); }
    void setOnSaveDevice(SaveDeviceCallback callback) { onSaveDevice = std::move(callback); }

private:
    void serveFile(const char* path, const char* contentType);

    // Sends JSON response ({ok:false, erro, mensagem}).
    void sendJson(int code, const JsonDocument& doc);
    void sendError(int code, const char* error, const String& message);
    bool readBody(JsonDocument& doc);

    // Validates Telegram prerequisites (token, Wi-Fi, clock).
    bool requireTelegram(const String& token);

    void handleStatus();
    void handleWifiNetworks();
    void handleWifiSave();
    void handleTokenTest();
    void handleTokenSave();
    void handleChats();
    void handleChatTest();
    void handleDevicesList();
    void handleDeviceSave();
    void handleDeviceDelete();
    void handlePendingList();
    void handlePendingDelete();

    WebServer server{80};
    SystemConfig& config;
    ConfigStorage& configStorage;
    std::function<void()> onWifiChanged;
    PendingPairingsProvider listPendingPairings;
    DiscardPendingCallback onDiscardPending;
    SaveDeviceCallback onSaveDevice;
};
