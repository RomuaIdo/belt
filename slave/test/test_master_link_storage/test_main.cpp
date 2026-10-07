// Validates MasterLinkStorage persistence across ESP.restart().
// Run: pio test -e esp32-s3-supermini-test -f test_master_link_storage (requires UART bridge).

#include <Arduino.h>
#include <unity.h>
#include <LittleFS.h>

#include "Domain/MasterLink.h"
#include "Storage/MasterLinkStorage.h"

namespace {

constexpr const char* kTestFilePath = "/test_master_link.json";
constexpr const char* kRebootMarkerPath = "/test_master_link_reboot_marker.txt";
constexpr const char* kMarkerSaveOk = "SAVE_OK";
constexpr const char* kMarkerSaveFailed = "SAVE_FAILED";
constexpr const char* kFakeMasterMac = "AA:BB:CC:DD:EE:01";
constexpr uint8_t kFakeMasterChannel = 6;

MasterLinkStorage* storage = nullptr;
String writePhaseMarker;

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

void test_write_phase_before_reboot_succeeded() {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kMarkerSaveOk, writePhaseMarker.c_str(),
                                       "save() reported failure before the reboot");
}

void test_reload_after_reboot_restores_the_mac_and_channel() {
    MasterLink loaded = storage->load();
    TEST_ASSERT_TRUE(loaded.hasMac());
    TEST_ASSERT_EQUAL_STRING(kFakeMasterMac, loaded.getMacAddress().c_str());
    TEST_ASSERT_TRUE(loaded.hasChannel());
    TEST_ASSERT_EQUAL_UINT8(kFakeMasterChannel, loaded.getChannel());
}

void test_out_of_range_channel_is_loaded_as_unknown() {
    File f = LittleFS.open(kTestFilePath, "w");
    TEST_ASSERT_TRUE(f);
    f.print("{\"masterMac\":\"AA:BB:CC:DD:EE:01\",\"channel\":200}");
    f.close();

    MasterLink loaded = storage->load();
    TEST_ASSERT_TRUE(loaded.hasMac());
    TEST_ASSERT_FALSE(loaded.hasChannel());
}

void test_clear_removes_the_persisted_file() {
    TEST_ASSERT_TRUE(storage->clear());

    MasterLink reloaded = storage->load();
    TEST_ASSERT_FALSE(reloaded.hasMac());
}

void setup() {
    delay(2000); // Allow serial monitor to attach
    Serial.begin(115200);

    if (!LittleFS.begin(true)) {
        Serial.println("FATAL: failed to mount LittleFS for tests");
        while (true) delay(1000);
    }

    static MasterLinkStorage linkStorage(kTestFilePath);
    storage = &linkStorage;

    if (!rebootMarkerExists()) {
        Serial.println("\n=== PHASE 1: writing a fake master MAC, then rebooting ===");

        storage->clear();
        bool ok = storage->save(MasterLink(kFakeMasterMac, kFakeMasterChannel));
        Serial.printf("MasterLinkStorage::save() -> %s\n", ok ? "OK" : "FAILED");

        writeRebootMarker(ok ? kMarkerSaveOk : kMarkerSaveFailed);

        Serial.println("Restarting ESP32 to validate persistence after reboot...");
        Serial.flush();
        delay(2000);
        ESP.restart();
        return;
    }

    Serial.println("\n=== PHASE 2: after reboot, validating the MAC reads back correctly ===");
    writePhaseMarker = readRebootMarker();

    UNITY_BEGIN();
    RUN_TEST(test_write_phase_before_reboot_succeeded);
    RUN_TEST(test_reload_after_reboot_restores_the_mac_and_channel);
    RUN_TEST(test_out_of_range_channel_is_loaded_as_unknown);
    RUN_TEST(test_clear_removes_the_persisted_file);
    UNITY_END();

    clearRebootMarker();
}

void loop() {}
