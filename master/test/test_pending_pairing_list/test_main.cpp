// Unit tests for PendingPairingList (belts waiting to be set up).
// Run: pio test -e esp32-s3-devkitc-1-test -f test_pending_pairing_list
// No Wi-Fi, credentials or flash needed.

#include <Arduino.h>
#include <unity.h>

#include "Domain/PendingPairingList.h"

namespace {
constexpr uint32_t kTtlMs = 15000;
constexpr const char* kMacA = "AA:BB:CC:DD:EE:01";
constexpr const char* kMacB = "AA:BB:CC:DD:EE:02";
}  // namespace

void test_heard_adds_a_belt_once_and_reports_it_as_new() {
    PendingPairingList list(kTtlMs);

    TEST_ASSERT_TRUE(list.heard(kMacA, 100, 1700000000));
    TEST_ASSERT_FALSE(list.heard(kMacA, 2100, 1700000002));

    auto entries = list.snapshot(2100);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(entries.size()));
    TEST_ASSERT_EQUAL_STRING(kMacA, entries[0].mac.c_str());
}

void test_heard_refreshes_the_timestamps() {
    PendingPairingList list(kTtlMs);

    list.heard(kMacA, 100, 1700000000);
    list.heard(kMacA, 5100, 1700000005);

    auto entries = list.snapshot(5100);
    TEST_ASSERT_EQUAL_UINT32(5100, entries[0].lastHeardMs);
    TEST_ASSERT_EQUAL_UINT32(1700000005, entries[0].receivedAtEpoch);
}

void test_mac_comparison_ignores_case() {
    PendingPairingList list(kTtlMs);

    list.heard("aa:bb:cc:dd:ee:01", 0, 0);
    TEST_ASSERT_FALSE(list.heard(kMacA, 10, 0));
    TEST_ASSERT_TRUE(list.contains("aa:bb:cc:dd:ee:01", 10));
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(list.snapshot(10).size()));
}

void test_a_silent_belt_expires_after_the_ttl() {
    PendingPairingList list(kTtlMs);

    list.heard(kMacA, 0, 0);
    TEST_ASSERT_TRUE(list.contains(kMacA, kTtlMs));       // exactly at the limit: still listed
    TEST_ASSERT_FALSE(list.contains(kMacA, kTtlMs + 1));  // past it: gone
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(list.snapshot(kTtlMs + 1).size()));
}

void test_a_repeating_belt_stays_listed() {
    PendingPairingList list(kTtlMs);

    for (uint32_t now = 0; now <= 60000; now += 2000) {
        list.heard(kMacA, now, 0);
    }
    TEST_ASSERT_TRUE(list.contains(kMacA, 60000));
}

void test_expiry_survives_millis_rollover() {
    PendingPairingList list(kTtlMs);

    const uint32_t nearWrap = 0xFFFFFFFFu - 1000;
    list.heard(kMacA, nearWrap, 0);
    TEST_ASSERT_TRUE(list.contains(kMacA, 2000));   // clock wrapped; only ~3 s elapsed
    TEST_ASSERT_FALSE(list.contains(kMacA, 20000)); // ~21 s elapsed
}

void test_remove_drops_the_belt() {
    PendingPairingList list(kTtlMs);

    list.heard(kMacA, 0, 0);
    list.heard(kMacB, 0, 0);

    TEST_ASSERT_TRUE(list.remove(kMacA));
    TEST_ASSERT_FALSE(list.remove(kMacA));
    TEST_ASSERT_FALSE(list.contains(kMacA, 0));
    TEST_ASSERT_TRUE(list.contains(kMacB, 0));
}

void test_snapshot_keeps_the_order_of_first_appearance() {
    PendingPairingList list(kTtlMs);

    list.heard(kMacA, 0, 0);
    list.heard(kMacB, 10, 0);
    list.heard(kMacA, 20, 0);  // refresh must not reorder

    auto entries = list.snapshot(20);
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(entries.size()));
    TEST_ASSERT_EQUAL_STRING(kMacA, entries[0].mac.c_str());
    TEST_ASSERT_EQUAL_STRING(kMacB, entries[1].mac.c_str());
}

void test_a_full_list_evicts_the_belt_silent_for_the_longest() {
    PendingPairingList list(kTtlMs);

    // Fill the list; the belt added first keeps being the quietest.
    char mac[18];
    for (size_t i = 0; i < PendingPairingList::kMaxEntries; ++i) {
        snprintf(mac, sizeof(mac), "AA:BB:CC:DD:00:%02X", static_cast<unsigned>(i));
        list.heard(mac, static_cast<uint32_t>(i * 100), 0);
    }
    TEST_ASSERT_EQUAL_UINT32(PendingPairingList::kMaxEntries,
                             static_cast<uint32_t>(list.snapshot(1000).size()));

    TEST_ASSERT_TRUE(list.heard(kMacA, 1000, 0));

    auto entries = list.snapshot(1000);
    TEST_ASSERT_EQUAL_UINT32(PendingPairingList::kMaxEntries, static_cast<uint32_t>(entries.size()));
    TEST_ASSERT_FALSE(list.contains("AA:BB:CC:DD:00:00", 1000));  // the oldest made room
    TEST_ASSERT_TRUE(list.contains("AA:BB:CC:DD:00:01", 1000));
    TEST_ASSERT_TRUE(list.contains(kMacA, 1000));
}

void setup() {
    delay(2000);  // Allow serial monitor to attach

    UNITY_BEGIN();
    RUN_TEST(test_heard_adds_a_belt_once_and_reports_it_as_new);
    RUN_TEST(test_heard_refreshes_the_timestamps);
    RUN_TEST(test_mac_comparison_ignores_case);
    RUN_TEST(test_a_silent_belt_expires_after_the_ttl);
    RUN_TEST(test_a_repeating_belt_stays_listed);
    RUN_TEST(test_expiry_survives_millis_rollover);
    RUN_TEST(test_remove_drops_the_belt);
    RUN_TEST(test_snapshot_keeps_the_order_of_first_appearance);
    RUN_TEST(test_a_full_list_evicts_the_belt_silent_for_the_longest);
    UNITY_END();
}

void loop() {}
