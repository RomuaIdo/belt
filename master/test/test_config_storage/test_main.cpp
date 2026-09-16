// Persistence config validation across a real reboot (ESP.restart()).
// Usage: pio test -e esp32-s3-devkitc-1-test -f test_config_storage
// Note: Requires a UART bridge (e.g., DevKitC-1). Native USB-CDC drops the Serial link on reset.

#include <Arduino.h>
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
    peerA.addPhone("11111111111");
    peerA.addPhone("22222222222");

    PeerNode peerB("AA:BB:CC:DD:EE:02", "Bob");
    peerB.addPhone("33333333333");

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

// ---- pre-reboot write phase ----

void test_write_phase_before_reboot_succeeded() {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kMarkerSaveOk, writePhaseMarker.c_str(),
                                       "ConfigStorage::save() reported failure before the reboot");
}

// ---- reconstructing the config after reboot ----

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
        TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(peerA->getPhones().size()));
        TEST_ASSERT_EQUAL_STRING("11111111111", peerA->getPhones()[0].c_str());
        TEST_ASSERT_EQUAL_STRING("22222222222", peerA->getPhones()[1].c_str());
    }

    const PeerNode* peerB = loaded.findPeerByMac("AA:BB:CC:DD:EE:02");
    TEST_ASSERT_NOT_NULL_MESSAGE(peerB, "peer AA:BB:CC:DD:EE:02 missing after reboot");
    if (peerB != nullptr) {
        TEST_ASSERT_EQUAL_STRING("Bob", peerB->getAlias().c_str());
        TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(peerB->getPhones().size()));
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
    delay(2000); // delay for the serial monitor to connect after a reset
    Serial.begin(115200);

    if (!LittleFS.begin(true)) {
        Serial.println("FATAL: failed to mount LittleFS for tests");
        while (true) delay(1000);
    }

    static ConfigStorage configStorage(kTestConfigPath);
    storage = &configStorage;

    if (!rebootMarkerExists()) {
        Serial.println("\n=== PHASE 1: witing fake config in flash ===");

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
    RUN_TEST(test_reload_after_reboot_restores_wifi_and_telegram_credentials);
    RUN_TEST(test_reload_after_reboot_restores_peers);
    RUN_TEST(test_reload_after_reboot_reports_configured);
    RUN_TEST(test_clear_removes_persisted_file);
    UNITY_END();

    clearRebootMarker();
}

void loop() {}
