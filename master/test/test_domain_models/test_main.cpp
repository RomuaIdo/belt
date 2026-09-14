// Unit tests for the pure in-memory domain model: PeerNode and SystemConfig.
// No flash/LittleFS access here — see test_config_storage for persistence.
// Run with: pio test -e esp32-s3-devkitc-1-test -f test_domain_models

#include <Arduino.h>
#include <unity.h>

#include "Domain/PeerNode.h"
#include "Domain/SystemConfig.h"

void test_peernode_add_phone_avoids_duplicates() {
    PeerNode peer("AA:BB:CC:DD:EE:FF", "Test");
    peer.addPhone("111111111");
    peer.addPhone("111111111"); // duplicate, must be ignored
    peer.addPhone("222222222");

    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(peer.getPhones().size()));
}

void test_peernode_remove_phone() {
    PeerNode peer("AA:BB:CC:DD:EE:FF", "Test");
    peer.addPhone("111111111");
    peer.addPhone("222222222");

    peer.removePhone("111111111");

    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(peer.getPhones().size()));
    TEST_ASSERT_EQUAL_STRING("222222222", peer.getPhones()[0].c_str());
}

void test_peernode_getters_and_alias() {
    PeerNode peer("AA:BB:CC:DD:EE:FF", "Nickname");

    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", peer.getMacAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("Nickname", peer.getAlias().c_str());

    peer.setAlias("New Nickname");
    TEST_ASSERT_EQUAL_STRING("New Nickname", peer.getAlias().c_str());
}

void test_systemconfig_add_and_find_peer() {
    SystemConfig config;
    PeerNode peer("11:22:33:44:55:66", "Grandma");

    TEST_ASSERT_TRUE(config.addPeer(peer));
    TEST_ASSERT_NOT_NULL(config.findPeerByMac("11:22:33:44:55:66"));
}

void test_systemconfig_find_peer_is_case_insensitive() {
    SystemConfig config;
    config.addPeer(PeerNode("AA:BB:CC:DD:EE:FF", "Grandma"));

    TEST_ASSERT_NOT_NULL(config.findPeerByMac("aa:bb:cc:dd:ee:ff"));
}

void test_systemconfig_rejects_duplicate_mac() {
    SystemConfig config;
    config.addPeer(PeerNode("11:22:33:44:55:66", "Grandma"));

    bool addedAgain = config.addPeer(PeerNode("11:22:33:44:55:66", "Another Alias"));

    TEST_ASSERT_FALSE(addedAgain);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(config.getPeers().size()));
}

void test_systemconfig_remove_peer() {
    SystemConfig config;
    config.addPeer(PeerNode("11:22:33:44:55:66", "Grandma"));

    TEST_ASSERT_TRUE(config.removePeer("11:22:33:44:55:66"));
    TEST_ASSERT_NULL(config.findPeerByMac("11:22:33:44:55:66"));
    TEST_ASSERT_FALSE(config.removePeer("11:22:33:44:55:66")); // already gone
}

void test_systemconfig_is_configured_requires_wifi_and_token() {
    SystemConfig config;
    TEST_ASSERT_FALSE(config.isConfigured());

    config.wifiSsid = "MySSID";
    TEST_ASSERT_FALSE(config.isConfigured());

    config.telegramBotToken = "123456:ABC-TOKEN";
    TEST_ASSERT_TRUE(config.isConfigured());
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output

    UNITY_BEGIN();
    RUN_TEST(test_peernode_add_phone_avoids_duplicates);
    RUN_TEST(test_peernode_remove_phone);
    RUN_TEST(test_peernode_getters_and_alias);
    RUN_TEST(test_systemconfig_add_and_find_peer);
    RUN_TEST(test_systemconfig_find_peer_is_case_insensitive);
    RUN_TEST(test_systemconfig_rejects_duplicate_mac);
    RUN_TEST(test_systemconfig_remove_peer);
    RUN_TEST(test_systemconfig_is_configured_requires_wifi_and_token);
    UNITY_END();
}

void loop() {}
