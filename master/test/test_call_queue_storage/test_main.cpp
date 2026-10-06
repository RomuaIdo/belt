// Validates CallQueueStorage persistence across ESP.restart() with power-loss simulation.
// Run: pio test -e esp32-s3-devkitc-1-test -f test_call_queue_storage (requires UART bridge).

#include <Arduino.h>
#include <ArduinoJson.h>
#include <unity.h>
#include <LittleFS.h>
#include <vector>

#include "Domain/Notification.h"
#include "Storage/CallQueueStorage.h"

namespace {

constexpr const char* kTestQueueDir = "/test_queue";
constexpr const char* kRebootMarkerPath = "/test_queue_reboot_marker.txt";
constexpr const char* kMarkerPhaseOk = "PHASE1_OK";
constexpr const char* kMarkerPhaseFailed = "PHASE1_FAILED";

CallQueueStorage* storage = nullptr;
String writePhaseMarker;

String eventIdA() {
    return Notification::makeEventId("AA:BB:CC:DD:EE:01", 1700000000);
}

String eventIdB() {
    return Notification::makeEventId("AA:BB:CC:DD:EE:02", 1700000100);
}

String corruptEventId() {
    return "corrupt_partial_write";
}

String orphanEventId() {
    return "orphan_already_delivered";
}

String pathForEvent(const String& eventId) {
    return String(kTestQueueDir) + "/evt_" + eventId + ".json";
}

Notification makeNotificationA() {
    std::vector<String> chatIds = {"123456789", "-1001234567890"};
    return Notification(eventIdA(), 1700000000, "AA:BB:CC:DD:EE:01", "Fall alert A", chatIds);
}

Notification makeNotificationB() {
    std::vector<String> chatIds = {"5000000000"};
    return Notification(eventIdB(), 1700000100, "AA:BB:CC:DD:EE:02", "Fall alert B", chatIds);
}

// Simulates power loss with a truncated JSON file.
void writeCorruptedQueueFile() {
    File f = LittleFS.open(pathForEvent(corruptEventId()), "w");
    if (f) {
        f.print("{\"eventId\":\"corrupt_partial_write\",\"timestamp\":1700000200,"
                 "\"originMac\":\"AA:BB:CC:DD:EE:99\",\"message\":\"Fall al");
        f.close();
    }
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

// Cleans test queue directory.
void resetQueueDir() {
    for (const auto& n : storage->loadAllPending()) storage->remove(n.getEventId());
    storage->purgeInvalidEntries();
}

} // namespace

// Pre-reboot write phase

void test_write_phase_before_reboot_succeeded() {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kMarkerPhaseOk, writePhaseMarker.c_str(),
                                       "enqueue()/updatePending() reported failure before the reboot");
}

void test_event_files_use_the_expected_naming_convention() {
    TEST_ASSERT_TRUE(LittleFS.exists(pathForEvent(eventIdA())));
    TEST_ASSERT_TRUE(LittleFS.exists(pathForEvent(eventIdB())));
}

// Post-reboot reconstruction

void test_saved_queue_uses_pending_chat_ids_as_strings() {
    const String eventIds[] = {eventIdA(), eventIdB()};
    const char* expectedChatIds[] = {"-1001234567890", "5000000000"};
    for (size_t i = 0; i < 2; ++i) {
        File file = LittleFS.open(pathForEvent(eventIds[i]), "r");
        TEST_ASSERT_TRUE(file);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        TEST_ASSERT_FALSE(error);

        JsonArrayConst pending = doc["pendingChatIds"].as<JsonArrayConst>();
        TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(pending.size()));
        TEST_ASSERT_TRUE(pending[0].is<String>());
        TEST_ASSERT_EQUAL_STRING(expectedChatIds[i], pending[0].as<const char*>());
        TEST_ASSERT_TRUE(doc["pendingPhones"].isNull());
    }
}

void test_reload_after_reboot_restores_only_the_valid_events() {
    auto pending = storage->loadAllPending();
    // Truncated file must be skipped.
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(pending.size()));
}

void test_reload_after_reboot_preserves_partial_update_progress() {
    auto pending = storage->loadAllPending();

    const Notification* found = nullptr;
    for (const auto& n : pending) {
        if (n.getEventId() == eventIdA()) {
            found = &n;
            break;
        }
    }

    TEST_ASSERT_NOT_NULL_MESSAGE(found, "event A missing after reboot");
    if (found != nullptr) {
        // Partial progress must persist across reboot.
        TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(found->getPendingChatIds().size()));
        TEST_ASSERT_EQUAL_STRING("-1001234567890", found->getPendingChatIds()[0].c_str());
        TEST_ASSERT_FALSE(found->isCompleted());
    }
}

void test_reload_after_reboot_restores_event_b_untouched() {
    auto pending = storage->loadAllPending();

    const Notification* found = nullptr;
    for (const auto& n : pending) {
        if (n.getEventId() == eventIdB()) {
            found = &n;
            break;
        }
    }

    TEST_ASSERT_NOT_NULL_MESSAGE(found, "event B missing after reboot");
    if (found != nullptr) {
        TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(found->getPendingChatIds().size()));
        TEST_ASSERT_EQUAL_STRING("5000000000", found->getPendingChatIds()[0].c_str());
    }
}

void test_queue_size_counts_every_file_including_the_corrupted_one() {
    // getQueueSize counts raw files without parsing.
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(storage->getQueueSize()));
}

void test_purge_invalid_entries_removes_only_the_corrupted_file() {
    // Only the corrupted file should be purged.
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->purgeInvalidEntries()));

    TEST_ASSERT_FALSE(LittleFS.exists(pathForEvent(corruptEventId())));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(storage->getQueueSize()));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(storage->loadAllPending().size()));

    // Purging is idempotent.
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(storage->purgeInvalidEntries()));
}

void test_completing_an_event_and_removing_it_shrinks_the_queue() {
    auto pending = storage->loadAllPending();
    Notification eventB;
    bool foundB = false;
    for (auto& n : pending) {
        if (n.getEventId() == eventIdB()) {
            eventB = n;
            foundB = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(foundB, "event B missing after reboot");

    eventB.markChatAsSent("5000000000");
    TEST_ASSERT_TRUE(eventB.isCompleted());
    TEST_ASSERT_TRUE(storage->remove(eventB.getEventId()));

    TEST_ASSERT_FALSE(LittleFS.exists(pathForEvent(eventIdB())));
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->loadAllPending().size()));
}

void test_purge_invalid_entries_removes_orphaned_completed_file() {
    std::vector<String> noPendingChatIds;
    Notification orphan(orphanEventId(), 1700000300, "AA:BB:CC:DD:EE:03", "already delivered", noPendingChatIds);
    TEST_ASSERT_TRUE(storage->updatePending(orphan));

    // Orphaned completed files remain on disk until purged.
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->loadAllPending().size()));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(storage->getQueueSize()));

    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->purgeInvalidEntries()));

    TEST_ASSERT_FALSE(LittleFS.exists(pathForEvent(orphanEventId())));
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->getQueueSize()));
    // Pending events remain untouched.
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->loadAllPending().size()));
}

void test_remove_on_missing_event_is_idempotent() {
    TEST_ASSERT_TRUE(storage->remove("does-not-exist"));
}

void setup() {
    delay(2000); // Allow serial monitor to attach
    Serial.begin(115200);

    if (!LittleFS.begin(true)) {
        Serial.println("FATAL: failed to mount LittleFS for tests");
        while (true) delay(1000);
    }

    static CallQueueStorage queueStorage(kTestQueueDir);
    storage = &queueStorage;

    if (!rebootMarkerExists()) {
        Serial.println("\n=== PHASE 1: enqueueing events + simulating a power-loss write, then rebooting ===");

        resetQueueDir();

        bool okA = storage->enqueue(makeNotificationA());
        bool okB = storage->enqueue(makeNotificationB());

        Notification partiallySentA = makeNotificationA();
        partiallySentA.markChatAsSent("123456789");
        bool okUpdate = storage->updatePending(partiallySentA);

        writeCorruptedQueueFile();

        bool ok = okA && okB && okUpdate;
        Serial.printf("enqueue A -> %s, enqueue B -> %s, updatePending A -> %s\n",
                       okA ? "OK" : "FAILED", okB ? "OK" : "FAILED", okUpdate ? "OK" : "FAILED");

        writeRebootMarker(ok ? kMarkerPhaseOk : kMarkerPhaseFailed);

        Serial.println("Restarting ESP32 to validate queue persistence after reboot...");
        Serial.flush();
        delay(2000);
        ESP.restart();
        return;
    }

    writePhaseMarker = readRebootMarker();

    if (writePhaseMarker != kMarkerPhaseOk && writePhaseMarker != kMarkerPhaseFailed) {
        // Reset state on stale reboot marker.
        Serial.printf("Stale/invalid reboot marker found (%s); resetting test state and re-running phase 1...\n",
                       writePhaseMarker.c_str());
        resetQueueDir();
        clearRebootMarker();
        Serial.flush();
        delay(500);
        ESP.restart();
        return;
    }

    Serial.println("\n=== PHASE 2: after reboot, validating the queue reads back without corruption ===");

    UNITY_BEGIN();
    RUN_TEST(test_write_phase_before_reboot_succeeded);
    RUN_TEST(test_event_files_use_the_expected_naming_convention);
    RUN_TEST(test_saved_queue_uses_pending_chat_ids_as_strings);
    RUN_TEST(test_reload_after_reboot_restores_only_the_valid_events);
    RUN_TEST(test_reload_after_reboot_preserves_partial_update_progress);
    RUN_TEST(test_reload_after_reboot_restores_event_b_untouched);
    RUN_TEST(test_queue_size_counts_every_file_including_the_corrupted_one);
    RUN_TEST(test_purge_invalid_entries_removes_only_the_corrupted_file);
    RUN_TEST(test_completing_an_event_and_removing_it_shrinks_the_queue);
    RUN_TEST(test_purge_invalid_entries_removes_orphaned_completed_file);
    RUN_TEST(test_remove_on_missing_event_is_idempotent);
    UNITY_END();

    resetQueueDir();
    clearRebootMarker();
}

void loop() {}
