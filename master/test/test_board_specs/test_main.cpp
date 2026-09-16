// Quick hardware-spec diagnostic, not a real unit test
// Run with: pio test -e esp32-s3-devkitc-1-test -f test_board_specs

#include <Arduino.h>
#include <unity.h>

namespace {
constexpr uint32_t kExpectedMinFlashBytes = 16UL * 1024 * 1024; // 16 MB
// ESP-IDF reserves ~2.4 KB of PSRAM for internal bookkeeping, reporting
// slightly under 8 MiB (~8,386,215 B). Using an 8,000,000 B threshold safely
// verifies an 8 MB chip while distinguishing it from smaller sizes (e.g., 2 MB).
constexpr uint32_t kExpectedMinPsramBytes = 8UL * 1000 * 1000;
} // namespace

void printBoardSpecs() {
    Serial.println("\n================ BOARD SPECS ================");
    Serial.printf("Chip model:          %s (rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("CPU cores:           %d\n", ESP.getChipCores());
    Serial.printf("CPU frequency:       %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("SDK version:         %s\n", ESP.getSdkVersion());
    Serial.println("-----------------------------------------------");
    Serial.printf("Flash size:          %u bytes (%.2f MB)\n",
                   ESP.getFlashChipSize(), ESP.getFlashChipSize() / (1024.0 * 1024.0));
    Serial.printf("Flash speed:         %u Hz\n", ESP.getFlashChipSpeed());
    Serial.printf("Flash mode:          %d\n", ESP.getFlashChipMode());
    Serial.printf("Sketch size:         %u bytes\n", ESP.getSketchSize());
    Serial.printf("Free sketch space:   %u bytes\n", ESP.getFreeSketchSpace());
    Serial.println("-----------------------------------------------");
    Serial.printf("PSRAM found:         %s\n", psramFound() ? "yes" : "no");
    Serial.printf("PSRAM size:          %u bytes (%.2f MB)\n",
                   ESP.getPsramSize(), ESP.getPsramSize() / (1024.0 * 1024.0));
    Serial.printf("Free PSRAM:          %u bytes\n", ESP.getFreePsram());
    Serial.println("-----------------------------------------------");
    Serial.printf("Internal heap size:  %u bytes\n", ESP.getHeapSize());
    Serial.printf("Free internal heap:  %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Min free heap ever:  %u bytes\n", ESP.getMinFreeHeap());
    Serial.println("===============================================\n");
}

void test_chip_is_esp32_s3() {
    TEST_ASSERT_EQUAL_STRING("ESP32-S3", ESP.getChipModel());
}

void test_flash_size_is_at_least_16mb() {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kExpectedMinFlashBytes, ESP.getFlashChipSize());
}

void test_psram_is_detected() {
    TEST_ASSERT_TRUE_MESSAGE(psramFound(),
        "PSRAM not detected -- check board_build.arduino.memory_type (must be qio_opi for N16R8)");
}

void test_psram_size_is_at_least_8mb() {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kExpectedMinPsramBytes, ESP.getPsramSize());
}

void setup() {
    delay(2000); // let the serial monitor attach before the first output
    Serial.begin(115200);

    printBoardSpecs();

    UNITY_BEGIN();
    RUN_TEST(test_chip_is_esp32_s3);
    RUN_TEST(test_flash_size_is_at_least_16mb);
    RUN_TEST(test_psram_is_detected);
    RUN_TEST(test_psram_size_is_at_least_8mb);
    UNITY_END();
}

void loop() {}
