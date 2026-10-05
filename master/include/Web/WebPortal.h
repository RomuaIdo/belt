#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <functional>
#include <vector>

#include "Domain/PendingPairing.h"
#include "Domain/SystemConfig.h"
#include "Storage/ConfigStorage.h"

// Master's configuration page: serves the frontend from LittleFS plus the
// JSON API it uses (Wi-Fi, Telegram token, belts, pairing). Runs entirely on
// the loop() task, so it shares `config` with AppController without any
// concurrency concerns. Routes that talk to Telegram, or that wait on a new
// belt's ESP-NOW confirmation, block loop() for a few seconds.
class WebPortal {
public:
    using PendingPairingsProvider = std::function<std::vector<PendingPairing>()>;
    using DiscardPendingCallback = std::function<void(const String& mac)>;
    // Validates and decides how to persist a belt. For a MAC never seen
    // before, this triggers (and waits on) the dashboard-initiated pairing
    // handshake (mode 2) before saving; an already-known or pending MAC
    // saves directly. Returns false if a new belt never confirmed in time.
    using SaveDeviceCallback = std::function<bool(const PeerNode& peer)>;

    // `onWifiChanged` runs after a new SSID/password is saved, to reconnect.
    WebPortal(SystemConfig& config, ConfigStorage& configStorage,
              std::function<void()> onWifiChanged);

    void begin();   // registers routes and starts the server on port 80
    void handle();  // services pending requests; call every loop()

    void setListPendingPairings(PendingPairingsProvider callback) { listPendingPairings = std::move(callback); }
    void setOnDiscardPending(DiscardPendingCallback callback) { onDiscardPending = std::move(callback); }
    void setOnSaveDevice(SaveDeviceCallback callback) { onSaveDevice = std::move(callback); }

private:
    void serveFile(const char* path, const char* contentType);

    // {ok:false, erro, mensagem} response. Expected Telegram outcomes
    // (bad token, no internet...) use code 200 so the frontend treats them
    // as results rather than failures.
    void sendJson(int code, const JsonDocument& doc);
    void sendError(int code, const char* error, const String& message);
    bool readBody(JsonDocument& doc);

    // Checks what Telegram needs (token, Wi-Fi, clock). Responds with the
    // matching error and returns false if anything is missing.
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
