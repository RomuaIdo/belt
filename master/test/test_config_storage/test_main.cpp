// Validates SystemConfig persistence across ESP.restart().
// Run: pio test -e esp32-s3-devkitc-1-test -f test_config_storage (requires UART bridge).

#include <Arduino.h>
#include <ArduinoJson.h>
#include <unity.h>
#include <LittleFS.h>

#include "Domain/SystemConfig.h"
#include "Storage/ConfigStorage.h"

namespace {

constexpr const char* kTestConfigPath = "/test_config.json";
constexpr const char* kRebootMarkerPath = "/test_reboot_marker.txt";
constexpr const char* kMarkerSaveOk = "SAVE_OK";
constexpr const char* kMarkerSaveFailed = "SAVE_FAILED";

ConfigStorage* storage = nullptr;
String writePhaseMarker;

SystemConfig buildFakeConfig() {
    SystemConfig config;
    config.wifiSsid = "TestSSID_Reboot";
    config.wifiPassword = "TestPass123!";
    config.telegramBotToken = "123456789:AAETestTokenXYZ";

    PeerNode peerA("AA:BB:CC:DD:EE:01", "Alice");
    peerA.addChatId("123456789");
    peerA.addChatId("-1001234567890");
    peerA.setMessage("Fall alert: {nome} at {hora}");

    PeerNode peerB("AA:BB:CC:DD:EE:02", "Bob");
    peerB.addChatId("5000000000");

    config.addPeer(peerA);
    config.addPeer(peerB);
    return config;
}

bool rebootMarkerExists() {
    return LittleFS.exists(kRebootMarkerPath);
}

String readRebootMarker() {
    File f = LittleFS.open(kRebootMarkerPath, "r");
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
}

void writeRebootMarker(const char* content) {
    File f = LittleFS.open(kRebootMarkerPath, "w");
    if (f) {
        f.print(content);
        f.close();
    }
}

void clearRebootMarker() {
    if (LittleFS.exists(kRebootMarkerPath)) LittleFS.remove(kRebootMarkerPath);
}

} // namespace

// Pre-reboot write phase

void test_write_phase_before_reboot_succeeded() {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kMarkerSaveOk, writePhaseMarker.c_str(),
                                       "ConfigStorage::save() reported failure before the reboot");
}

// Post-reboot reconstruction

void test_saved_config_uses_chat_ids_as_strings() {
    File file = LittleFS.open(kTestConfigPath, "r");
    TEST_ASSERT_TRUE(file);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    TEST_ASSERT_FALSE(error);

    JsonArrayConst peers = doc["peers"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(peers.size()));
    JsonArrayConst chatIdsA = peers[0]["chatIds"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(chatIdsA.size()));
    TEST_ASSERT_TRUE(chatIdsA[0].is<String>());
    TEST_ASSERT_TRUE(chatIdsA[1].is<String>());
    TEST_ASSERT_EQUAL_STRING("123456789", chatIdsA[0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("-1001234567890", chatIdsA[1].as<const char*>());
    JsonArrayConst chatIdsB = peers[1]["chatIds"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(chatIdsB.size()));
    TEST_ASSERT_TRUE(chatIdsB[0].is<String>());
    TEST_ASSERT_EQUAL_STRING("5000000000", chatIdsB[0].as<const char*>());
    TEST_ASSERT_TRUE(peers[0]["phones"].isNull());
    TEST_ASSERT_TRUE(peers[1]["phones"].isNull());
}

void test_reload_after_reboot_restores_wifi_and_telegram_credentials() {
    SystemConfig loaded = storage->load();
    TEST_ASSERT_EQUAL_STRING("TestSSID_Reboot", loaded.wifiSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("TestPass123!", loaded.wifiPassword.c_str());
    TEST_ASSERT_EQUAL_STRING("123456789:AAETestTokenXYZ", loaded.telegramBotToken.c_str());
}

void test_reload_after_reboot_restores_peers() {
    SystemConfig loaded = storage->load();
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(loaded.getPeers().size()));

    const PeerNode* peerA = loaded.findPeerByMac("AA:BB:CC:DD:EE:01");
    TEST_ASSERT_NOT_NULL_MESSAGE(peerA, "peer AA:BB:CC:DD:EE:01 missing after reboot");
    if (peerA != nullptr) {
        TEST_ASSERT_EQUAL_STRING("Alice", peerA->getAlias().c_str());
        TEST_ASSERT_EQUAL_STRING("Fall alert: {nome} at {hora}", peerA->getMessage().c_str());
        TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(peerA->getChatIds().size()));
        TEST_ASSERT_EQUAL_STRING("123456789", peerA->getChatIds()[0].c_str());
        TEST_ASSERT_EQUAL_STRING("-1001234567890", peerA->getChatIds()[1].c_str());
    }

    const PeerNode* peerB = loaded.findPeerByMac("AA:BB:CC:DD:EE:02");
    TEST_ASSERT_NOT_NULL_MESSAGE(peerB, "peer AA:BB:CC:DD:EE:02 missing after reboot");
    if (peerB != nullptr) {
        TEST_ASSERT_EQUAL_STRING("Bob", peerB->getAlias().c_str());
        TEST_ASSERT_EQUAL_STRING("", peerB->getMessage().c_str()); // Default message
        TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(peerB->getChatIds().size()));
        TEST_ASSERT_EQUAL_STRING("5000000000", peerB->getChatIds()[0].c_str());
    }
}

void test_reload_after_reboot_reports_configured() {
    SystemConfig loaded = storage->load();
    TEST_ASSERT_TRUE(loaded.isConfigured());
}

void test_clear_removes_persisted_file() {
    TEST_ASSERT_TRUE(storage->clear());

    SystemConfig reloaded = storage->load();
    TEST_ASSERT_EQUAL_STRING("", reloaded.wifiSsid.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(reloaded.getPeers().size()));
}

void setup() {
    delay(2000); // Allow serial monitor to attach
    Serial.begin(115200);

    if (!LittleFS.begin(true)) {
        Serial.println("FATAL: failed to mount LittleFS for tests");
        while (true) delay(1000);
    }

    static ConfigStorage configStorage(kTestConfigPath);
    storage = &configStorage;

    if (!rebootMarkerExists()) {
        Serial.println("\n=== PHASE 1: writing fake config in flash ===");

        storage->clear();
        SystemConfig fake = buildFakeConfig();
        bool ok = storage->save(fake);
        Serial.printf("ConfigStorage::save() -> %s\n", ok ? "OK" : "FAILED");

        writeRebootMarker(ok ? kMarkerSaveOk : kMarkerSaveFailed);

        Serial.println("Restarting ESP32 to validate persistence after reboot...");
        Serial.flush();
        delay(2000);
        ESP.restart();
        return;
    }

    Serial.println("\n=== PHASE 2: after reboot, validating the reconstruction of the objects ===");
    writePhaseMarker = readRebootMarker();

    UNITY_BEGIN();
    RUN_TEST(test_write_phase_before_reboot_succeeded);
    RUN_TEST(test_saved_config_uses_chat_ids_as_strings);
    RUN_TEST(test_reload_after_reboot_restores_wifi_and_telegram_credentials);
    RUN_TEST(test_reload_after_reboot_restores_peers);
    RUN_TEST(test_reload_after_reboot_reports_configured);
    RUN_TEST(test_clear_removes_persisted_file);
    UNITY_END();

    clearRebootMarker();
}

void loop() {}
