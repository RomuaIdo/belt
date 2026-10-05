#include "Web/WebPortal.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <uri/UriBraces.h>
#include <algorithm>
#include <time.h>
#include <vector>

#include "Messaging/TelegramTask.h"
#include "Util/Clock.h"
#include "Util/MacUtils.h"

namespace {
constexpr size_t MAX_SSID_LENGTH = 32;
constexpr size_t MIN_PASSWORD_LENGTH = 8;   // WPA2; empty = open network
constexpr size_t MAX_PASSWORD_LENGTH = 63;
constexpr size_t MAX_NETWORKS = 20;

constexpr size_t MAX_NAME_LENGTH = 40;
constexpr size_t MAX_MESSAGE_LENGTH = 300;
constexpr size_t MAX_CHATS_PER_DEVICE = 10;

const char* const TEST_MESSAGE = "Cinto Alerta: test message. If you can read this, alerts will reach you.";

// Telegram chat ID (negative for groups).
bool isChatId(const String& text) {
    const size_t start = text.startsWith("-") ? 1 : 0;
    if (text.length() <= start || text.length() > start + 20) return false;
    for (size_t i = start; i < text.length(); ++i) {
        if (!isDigit(text[i])) return false;
    }
    return true;
}

// BotFather token format validation.
bool looksLikeBotToken(const String& token) {
    if (token.length() < 10 || token.length() > 100 || token.indexOf(':') < 1) return false;
    for (size_t i = 0; i < token.length(); ++i) {
        if (isSpace(token[i])) return false;
    }
    return true;
}

bool isSuccess(int httpStatus) { return httpStatus >= 200 && httpStatus < 300; }
bool isBadToken(int httpStatus) { return httpStatus == 401 || httpStatus == 404; }

// ISO 8601 UTC timestamp for frontend clock comparison.
String toIso8601Utc(time_t epoch) {
    struct tm utc;
    char buf[25];
    gmtime_r(&epoch, &utc);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return String(buf);
}
}

WebPortal::WebPortal(SystemConfig& config, ConfigStorage& configStorage,
                     std::function<void()> onWifiChanged)
    : config(config), configStorage(configStorage), onWifiChanged(std::move(onWifiChanged)) {}

void WebPortal::begin() {
    // Serve frontend files individually to avoid exposing config.json.
    server.on("/", HTTP_GET, [this] { serveFile("/index.html", "text/html"); });
    server.on("/index.html", HTTP_GET, [this] { serveFile("/index.html", "text/html"); });
    server.on("/style.css", HTTP_GET, [this] { serveFile("/style.css", "text/css"); });
    server.on("/app.js", HTTP_GET, [this] { serveFile("/app.js", "text/javascript"); });

    server.on("/api/status", HTTP_GET, [this] { handleStatus(); });
    server.on("/api/wifi/redes", HTTP_GET, [this] { handleWifiNetworks(); });
    server.on("/api/wifi", HTTP_POST, [this] { handleWifiSave(); });
    server.on("/api/telegram/testar-token", HTTP_POST, [this] { handleTokenTest(); });
    server.on("/api/telegram/token", HTTP_PUT, [this] { handleTokenSave(); });
    server.on("/api/telegram/conversas", HTTP_GET, [this] { handleChats(); });
    server.on("/api/teste-telegram", HTTP_POST, [this] { handleChatTest(); });

    server.on("/api/dispositivos", HTTP_GET, [this] { handleDevicesList(); });
    server.on(UriBraces("/api/dispositivos/{}"), HTTP_PUT, [this] { handleDeviceSave(); });
    server.on(UriBraces("/api/dispositivos/{}"), HTTP_DELETE, [this] { handleDeviceDelete(); });

    server.on("/api/pendentes", HTTP_GET, [this] { handlePendingList(); });
    server.on(UriBraces("/api/pendentes/{}"), HTTP_DELETE, [this] { handlePendingDelete(); });

    server.onNotFound([this] { sendError(404, "nao_encontrado", "Not found."); });

    server.begin();
    Serial.println("WebPortal: servidor web na porta 80");
}

void WebPortal::handle() {
    server.handleClient();
}

// Helpers

void WebPortal::serveFile(const char* path, const char* contentType) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        server.send(404, "text/plain", "Frontend not installed. Run: pio run -t uploadfs");
        return;
    }
    server.sendHeader("Cache-Control", "no-cache");
    server.streamFile(file, contentType);
    file.close();
}

void WebPortal::sendJson(int code, const JsonDocument& doc) {
    String body;
    serializeJson(doc, body);
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", body);
}

void WebPortal::sendError(int code, const char* error, const String& message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["erro"] = error;
    doc["mensagem"] = message;
    sendJson(code, doc);
}

bool WebPortal::readBody(JsonDocument& doc) {
    if (deserializeJson(doc, server.arg("plain"))) {
        sendError(400, "corpo_invalido", "Invalid request body.");
        return false;
    }
    return true;
}

bool WebPortal::requireTelegram(const String& token) {
    if (token.isEmpty()) {
        sendError(200, "sem_token", "No Telegram bot token is set.");
    } else if (WiFi.status() != WL_CONNECTED) {
        sendError(200, "sem_internet", "The master is not connected to the internet.");
    } else if (!isClockValid()) {
        sendError(200, "sem_relogio", "The master is still syncing its clock. Try again in a few seconds.");
    } else {
        return true;
    }
    return false;
}

// Status and Wi-Fi

void WebPortal::handleStatus() {
    const bool connected = WiFi.status() == WL_CONNECTED;

    JsonDocument doc;
    doc["wifi"]["ssid"] = config.wifiSsid;
    doc["wifi"]["conectado"] = connected;
    doc["wifi"]["ip"] = connected ? WiFi.localIP().toString() : String();
    doc["telegram"]["configurado"] = !config.telegramBotToken.isEmpty();
    doc["relogio_ok"] = isClockValid();
    sendJson(200, doc);
}

void WebPortal::handleWifiNetworks() {
    struct Network {
        String ssid;
        int rssi;
        bool open;
    };

    const int count = WiFi.scanNetworks();  // Blocking (2-3s)
    if (count < 0) {
        sendError(500, "erro_scan", "Could not scan for networks. Try again.");
        return;
    }

    // Deduplicate BSSIDs by keeping strongest RSSI.
    std::vector<Network> networks;
    for (int i = 0; i < count; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // Ignore hidden networks

        auto same = std::find_if(networks.begin(), networks.end(),
                                 [&](const Network& n) { return n.ssid == ssid; });
        if (same == networks.end()) {
            networks.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
        } else if (WiFi.RSSI(i) > same->rssi) {
            same->rssi = WiFi.RSSI(i);
        }
    }
    WiFi.scanDelete();

    std::sort(networks.begin(), networks.end(),
              [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
    if (networks.size() > MAX_NETWORKS) networks.resize(MAX_NETWORKS);

    JsonDocument doc;
    JsonArray list = doc["redes"].to<JsonArray>();
    for (const auto& network : networks) {
        JsonObject item = list.add<JsonObject>();
        item["ssid"] = network.ssid;
        item["rssi"] = network.rssi;
        item["aberta"] = network.open;
    }
    sendJson(200, doc);
}

void WebPortal::handleWifiSave() {
    JsonDocument body;
    if (!readBody(body)) return;

    const String ssid = body["ssid"] | "";
    const String password = body["senha"] | "";
    if (ssid.isEmpty() || ssid.length() > MAX_SSID_LENGTH) {
        sendError(400, "ssid_invalido", "Enter the network name.");
        return;
    }
    if (!password.isEmpty() &&
        (password.length() < MIN_PASSWORD_LENGTH || password.length() > MAX_PASSWORD_LENGTH)) {
        sendError(400, "senha_invalida", "The Wi-Fi password must have 8 to 63 characters.");
        return;
    }

    config.wifiSsid = ssid;
    config.wifiPassword = password;
    if (!configStorage.save(config)) {
        sendError(500, "erro_gravacao", "Could not save the settings.");
        return;
    }

    onWifiChanged();
    JsonDocument doc;
    doc["ok"] = true;
    sendJson(200, doc);
}

// Telegram

void WebPortal::handleTokenTest() {
    JsonDocument body;
    if (!readBody(body)) return;

    String token = body["token"] | "";
    token.trim();
    if (!requireTelegram(token)) return;

    String response;
    const int status = TelegramTask::request(token, "getMe", "{}", &response);
    if (status == 200) {
        JsonDocument reply;
        deserializeJson(reply, response);
        JsonDocument doc;
        doc["ok"] = true;
        doc["bot"] = String("@") + (reply["result"]["username"] | "");
        sendJson(200, doc);
    } else if (isBadToken(status)) {
        sendError(200, "token_invalido", "Telegram did not accept this token.");
    } else {
        sendError(200, "sem_internet", "Could not reach Telegram (status " + String(status) + ").");
    }
}

void WebPortal::handleTokenSave() {
    JsonDocument body;
    if (!readBody(body)) return;

    String token = body["token"] | "";
    token.trim();
    if (!looksLikeBotToken(token)) {
        sendError(400, "token_invalido", "That does not look like a bot token.");
        return;
    }

    config.telegramBotToken = token;
    if (!configStorage.save(config)) {
        sendError(500, "erro_gravacao", "Could not save the settings.");
        return;
    }

    JsonDocument doc;
    doc["ok"] = true;
    sendJson(200, doc);
}

void WebPortal::handleChats() {
    if (!requireTelegram(config.telegramBotToken)) return;

    String response;
    const int status = TelegramTask::request(
        config.telegramBotToken, "getUpdates",
        "{\"limit\":100,\"allowed_updates\":[\"message\"]}", &response);
    if (isBadToken(status)) {
        sendError(200, "token_invalido", "Telegram did not accept the saved token.");
        return;
    }
    if (status != 200) {
        sendError(200, "sem_internet", "Could not reach Telegram (status " + String(status) + ").");
        return;
    }

    // Extract only chat objects from response.
    JsonDocument filter;
    filter["result"][0]["message"]["chat"] = true;
    JsonDocument reply;
    if (deserializeJson(reply, response, DeserializationOption::Filter(filter))) {
        sendError(200, "erro_telegram", "Unexpected reply from Telegram.");
        return;
    }

    JsonDocument doc;
    doc["ok"] = true;
    JsonArray chats = doc["conversas"].to<JsonArray>();
    std::vector<long long> seen;
    for (JsonObject update : reply["result"].as<JsonArray>()) {
        JsonObject chat = update["message"]["chat"];
        if (chat.isNull()) continue;

        const long long id = chat["id"].as<long long>();
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back(id);

        String name = chat["title"] | "";  // Groups
        if (name.isEmpty()) {
            name = chat["first_name"] | "";
            const String last = chat["last_name"] | "";
            if (!last.isEmpty()) name += " " + last;
        }
        if (name.isEmpty()) name = chat["username"] | "";
        if (name.isEmpty()) name = "(no name)";

        char idText[24];
        snprintf(idText, sizeof(idText), "%lld", id);
        JsonObject item = chats.add<JsonObject>();
        item["chat_id"] = idText;
        item["nome"] = name;
    }
    sendJson(200, doc);
}

void WebPortal::handleChatTest() {
    JsonDocument body;
    if (!readBody(body)) return;

    const String chatId = body["chat_id"] | "";
    if (!isChatId(chatId)) {
        sendError(200, "chat_id_invalido", "The chat id must be a number.");
        return;
    }
    if (!requireTelegram(config.telegramBotToken)) return;

    const int status = TelegramTask::request(
        config.telegramBotToken, "sendMessage",
        TelegramTask::createJsonPayload(chatId, TEST_MESSAGE));
    if (isSuccess(status)) {
        JsonDocument doc;
        doc["ok"] = true;
        sendJson(200, doc);
    } else if (status == 400 || status == 403) {
        sendError(200, "chat_desconhecido", "Telegram rejected this chat.");
    } else if (isBadToken(status)) {
        sendError(200, "token_invalido", "Telegram did not accept the saved token.");
    } else if (status < 0) {
        sendError(200, "sem_internet", "Could not reach Telegram.");
    } else {
        sendError(200, "erro_telegram", "Telegram replied with status " + String(status) + ".");
    }
}

// Devices

void WebPortal::handleDevicesList() {
    JsonDocument doc;
    JsonArray devices = doc["dispositivos"].to<JsonArray>();
    for (const auto& peer : config.getPeers()) {
        JsonObject item = devices.add<JsonObject>();
        item["mac"] = peer.getMacAddress();
        item["nome"] = peer.getAlias();
        item["mensagem"] = peer.getMessage();
        JsonArray chatIds = item["chat_ids"].to<JsonArray>();
        for (const auto& chatId : peer.getChatIds()) {
            chatIds.add(chatId);
        }
    }
    sendJson(200, doc);
}

void WebPortal::handleDeviceSave() {
    uint8_t bytes[6];
    String mac = server.urlDecode(server.pathArg(0));
    if (!MacUtils::parse(mac, bytes)) {
        sendError(400, "mac_invalido", "The belt address is not valid.");
        return;
    }
    mac = MacUtils::format(bytes);  // Uppercase

    JsonDocument body;
    if (!readBody(body)) return;

    const String name = body["nome"] | "";
    const String message = body["mensagem"] | "";
    JsonArray chatIds = body["chat_ids"].as<JsonArray>();
    if (name.isEmpty() || name.length() > MAX_NAME_LENGTH) {
        sendError(400, "nome_invalido", "Enter the person's name.");
        return;
    }
    if (message.length() > MAX_MESSAGE_LENGTH) {
        sendError(400, "mensagem_invalida", "The message is too long.");
        return;
    }
    if (chatIds.isNull() || chatIds.size() > MAX_CHATS_PER_DEVICE) {
        sendError(400, "chats_invalidos", "Choose who receives the alerts.");
        return;
    }

    PeerNode peer(mac, name);
    peer.setMessage(message);
    for (JsonVariant item : chatIds) {
        const String chatId = item | "";
        if (!isChatId(chatId)) {
            sendError(400, "chats_invalidos", "A chat id is not valid.");
            return;
        }
        peer.addChatId(chatId);
    }

    // For new devices, waits for ESP-NOW pairing confirmation before saving.
    if (!onSaveDevice || !onSaveDevice(peer)) {
        sendError(200, "sem_confirmacao_do_cinto",
                  "Could not confirm pairing with the belt. Press its pairing button, or try again.");
        return;
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["confirmado_pelo_cinto"] = true;
    sendJson(200, doc);
}

void WebPortal::handleDeviceDelete() {
    const String mac = server.urlDecode(server.pathArg(0));
    if (!config.removePeer(mac)) {
        sendError(404, "nao_encontrado", "Belt not found.");
        return;
    }
    if (!configStorage.save(config)) {
        sendError(500, "erro_gravacao", "Could not save the settings.");
        return;
    }

    JsonDocument doc;
    doc["ok"] = true;
    sendJson(200, doc);
}

// Pairing

void WebPortal::handlePendingList() {
    JsonDocument doc;
    JsonArray arr = doc["pendentes"].to<JsonArray>();
    if (listPendingPairings) {
        for (const auto& pending : listPendingPairings()) {
            JsonObject item = arr.add<JsonObject>();
            item["mac"] = pending.mac;
            item["recebido_em"] = toIso8601Utc(static_cast<time_t>(pending.receivedAtEpoch));
        }
    }
    sendJson(200, doc);
}

void WebPortal::handlePendingDelete() {
    const String mac = server.urlDecode(server.pathArg(0));
    if (onDiscardPending) onDiscardPending(mac);

    JsonDocument doc;
    doc["ok"] = true;
    sendJson(200, doc);
}
