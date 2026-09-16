// Persistence validation for CallQueueStorage across a real reboot (ESP.restart()), including a simulated power-loss.
// Run with: pio test -e esp32-s3-devkitc-1-test -f test_call_queue_storage
// Note: Requires a UART bridge (e.g., DevKitC-1). Native USB-CDC drops the Serial link on reset.

#include <Arduino.h>
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
    std::vector<String> phones = {"11111111111", "22222222222"};
    return Notification(eventIdA(), 1700000000, "AA:BB:CC:DD:EE:01", "Fall alert A", phones);
}

Notification makeNotificationB() {
    std::vector<String> phones = {"33333333333"};
    return Notification(eventIdB(), 1700000100, "AA:BB:CC:DD:EE:02", "Fall alert B", phones);
}


// Simulates power loss mid-write: a truncated, unclosed JSON prefix.
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

// Wipes every file in the test queue directory, valid, corrupted or
// orphaned, so each `pio test` run starts from a clean slate.
void resetQueueDir() {
    for (const auto& n : storage->loadAllPending()) storage->remove(n.getEventId());
    storage->purgeInvalidEntries();
}

} // namespace

// ---- pre-reboot write phase ----

void test_write_phase_before_reboot_succeeded() {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kMarkerPhaseOk, writePhaseMarker.c_str(),
                                       "enqueue()/updatePending() reported failure before the reboot");
}

void test_event_files_use_the_expected_naming_convention() {
    TEST_ASSERT_TRUE(LittleFS.exists(pathForEvent(eventIdA())));
    TEST_ASSERT_TRUE(LittleFS.exists(pathForEvent(eventIdB())));
}

// ---- reconstructing the queue after reboot ----

void test_reload_after_reboot_restores_only_the_valid_events() {
    auto pending = storage->loadAllPending();
    // 2 valid events; the truncated third file must be silently skipped.
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
        // updatePending() ran before the simulated crash and already marked
        // the first phone as sent; that partial progress must survive.
        TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(found->getPendingPhones().size()));
        TEST_ASSERT_EQUAL_STRING("22222222222", found->getPendingPhones()[0].c_str());
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
        TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(found->getPendingPhones().size()));
        TEST_ASSERT_EQUAL_STRING("33333333333", found->getPendingPhones()[0].c_str());
    }
}

void test_queue_size_counts_every_file_including_the_corrupted_one() {
    // getQueueSize() is a cheap directory listing (no JSON parsing), so it also counts the unparseable file. 
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(storage->getQueueSize()));
}

void test_purge_invalid_entries_removes_only_the_corrupted_file() {
    // A and B are still genuinely pending at this point; only the truncated
    // third file should be considered garbage.
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->purgeInvalidEntries()));

    TEST_ASSERT_FALSE(LittleFS.exists(pathForEvent(corruptEventId())));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(storage->getQueueSize()));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(storage->loadAllPending().size()));

    // Purging again finds nothing left to remove: it's safe to call
    // repeatedly (e.g. on every boot) without touching valid entries.
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

    eventB.markPhoneAsSent("33333333333");
    TEST_ASSERT_TRUE(eventB.isCompleted());
    TEST_ASSERT_TRUE(storage->remove(eventB.getEventId()));

    TEST_ASSERT_FALSE(LittleFS.exists(pathForEvent(eventIdB())));
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->loadAllPending().size()));
}

void test_purge_invalid_entries_removes_orphaned_completed_file() {
    std::vector<String> noPendingPhones;
    Notification orphan(orphanEventId(), 1700000300, "AA:BB:CC:DD:EE:03", "already delivered", noPendingPhones);
    TEST_ASSERT_TRUE(storage->updatePending(orphan));

    // loadAllPending() already excludes it (nothing left to deliver), but
    // the file is still sitting on disk, unlike a properly-removed event.
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->loadAllPending().size()));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(storage->getQueueSize()));

    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->purgeInvalidEntries()));

    TEST_ASSERT_FALSE(LittleFS.exists(pathForEvent(orphanEventId())));
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->getQueueSize()));
    // Event A, still genuinely pending, must be untouched by the purge.
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(storage->loadAllPending().size()));
}

void test_remove_on_missing_event_is_idempotent() {
    TEST_ASSERT_TRUE(storage->remove("does-not-exist"));
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output
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
        partiallySentA.markPhoneAsSent("11111111111");
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
        return; // unreachable, kept for clarity
    }

    writePhaseMarker = readRebootMarker();

    if (writePhaseMarker != kMarkerPhaseOk && writePhaseMarker != kMarkerPhaseFailed) {
    // Flash remnants from an interrupted run (pio test doesn't wipe LittleFS).
    // Treat unknown markers as stale data, clean up, and restart phase 1.
        Serial.printf("Stale/invalid reboot marker found (%s); resetting test state and re-running phase 1...\n",
                       writePhaseMarker.c_str());
        resetQueueDir();
        clearRebootMarker();
        Serial.flush();
        delay(500);
        ESP.restart();
        return; // unreachable, kept for clarity
    }

    Serial.println("\n=== PHASE 2: after reboot, validating the queue reads back without corruption ===");

    UNITY_BEGIN();
    RUN_TEST(test_write_phase_before_reboot_succeeded);
    RUN_TEST(test_event_files_use_the_expected_naming_convention);
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
