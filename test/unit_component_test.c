#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"  // For Wi-Fi Component Test
#include "ff.h"               // For SD Component Test

// Include project headers
#include "cs04_coap_packet.h"
#include "cs04_coap_reliability.h"
#include "cs04_hardware.h"

// --- WI-FI CREDENTIALS ---
#define TEST_WIFI_SSID "lomohomo"
#define TEST_WIFI_PASS "K0piP3ng"

// ==========================================
// TEST FRAMEWORK
// ==========================================
int tests_total = 0;
int tests_passed = 0;
int tests_failed = 0;

#define TEST_ASSERT(cond, msg)                              \
    do {                                                    \
        tests_total++;                                      \
        if (cond) {                                         \
            printf("[PASS] %s\n", msg);                     \
            tests_passed++;                                 \
        } else {                                            \
            printf("[FAIL] %s (Line %d)\n", msg, __LINE__); \
            tests_failed++;                                 \
        }                                                   \
    } while (0)

#define TEST_ASSERT_EQUAL_HEX(expected, actual, msg)                    \
    do {                                                                \
        tests_total++;                                                  \
        if ((expected) == (actual)) {                                   \
            printf("[PASS] %s\n", msg);                                 \
            tests_passed++;                                             \
        } else {                                                        \
            printf("[FAIL] %s: Expected 0x%X, got 0x%X\n", msg,         \
                   (unsigned int) (expected), (unsigned int) (actual)); \
            tests_failed++;                                             \
        }                                                               \
    } while (0)

// ==========================================
// PART 1: UNIT TESTS (Pure Logic)
// ==========================================

void unit_test_packet_msg_id()
{
    printf("\n[UNIT] Testing Message ID Extraction...\n");
    coap_packet_t pkt;
    pkt.hdr.id[0] = 0xAB;
    pkt.hdr.id[1] = 0xCD;
    uint16_t result = coap_extract_msg_id(&pkt);
    TEST_ASSERT_EQUAL_HEX(0xABCD, result, "Extract Message ID 0xABCD");
}

void unit_test_token_matching()
{
    printf("\n[UNIT] Testing Token Matching...\n");
    coap_buffer_t t1, t2, t3;
    uint8_t d1[] = { 0xCA, 0xFE };
    uint8_t d3[] = { 0xBE, 0xEF };

    t1.p = d1;
    t1.len = 2;
    t2.p = d1;
    t2.len = 2;
    t3.p = d3;
    t3.len = 2;

    TEST_ASSERT(coap_token_matches(&t1, &t2) == true, "Identical tokens match");
    TEST_ASSERT(coap_token_matches(&t1, &t3) == false,
                "Different tokens do not match");
}

void unit_test_block_size_math()
{
    printf("\n[UNIT] Testing Block Size Math...\n");
    TEST_ASSERT(coap_block_size_from_szx(0) == 16, "SZX 0 -> 16 bytes");
    TEST_ASSERT(coap_block_size_from_szx(6) == 1024, "SZX 6 -> 1024 bytes");
    TEST_ASSERT(coap_block_size_from_szx(7) == 1024,
                "SZX 7 (Max) -> 1024 bytes");
}

void unit_test_block2_encoding()
{
    printf("\n[UNIT] Testing Block2 Option Encoding...\n");
    uint8_t buf[3] = { 0 };

    // Case 1: Block 0, More 1, SZX 6 -> 0x0E
    size_t len = coap_encode_block2_option(buf, 0, true, 6);
    TEST_ASSERT(len == 1, "Short Block2 should be 1 byte");
    TEST_ASSERT_EQUAL_HEX(0x0E, buf[0], "Encoded Byte Match");

    // Case 2: Block 100, More 0, SZX 6 -> 0x0646
    len = coap_encode_block2_option(buf, 100, false, 6);
    TEST_ASSERT(len == 2, "Medium Block2 should be 2 bytes");
    TEST_ASSERT_EQUAL_HEX(0x06, buf[0], "Upper Byte Match");
    TEST_ASSERT_EQUAL_HEX(0x46, buf[1], "Lower Byte Match");
}

void unit_test_reliability_basic()
{
    printf("\n[UNIT] Testing Duplicate Detection...\n");
    duplicate_detector_t det;
    coap_duplicate_detector_init(&det);

    TEST_ASSERT(coap_is_duplicate_message(&det, 123) == false,
                "New ID 123 is not duplicate");
    coap_record_message_id(&det, 123);
    TEST_ASSERT(coap_is_duplicate_message(&det, 123) == true,
                "ID 123 is now duplicate");
}

void unit_test_reliability_circular_buffer()
{
    printf("\n[UNIT] Testing Circular Buffer Overwrite...\n");
    duplicate_detector_t det;
    coap_duplicate_detector_init(&det);

    // Fill buffer (size 16)
    for (int i = 1; i <= 16; i++) {
        coap_record_message_id(&det, i);
    }

    // Oldest (1) should still be there
    TEST_ASSERT(coap_is_duplicate_message(&det, 1) == true,
                "Oldest ID (1) present");

    // Add 17th item -> Should overwrite 1
    coap_record_message_id(&det, 17);

    TEST_ASSERT(coap_is_duplicate_message(&det, 1) == false,
                "ID 1 overwritten");
    TEST_ASSERT(coap_is_duplicate_message(&det, 17) == true, "ID 17 present");
}

void unit_test_led_math()
{
    printf("\n[UNIT] Testing LED Color Math...\n");
    // Brightness 0.5: Input 100 -> Expected 50
    uint32_t result = hw_urgb_u32(100, 100, 100, 0.5f);

    uint32_t expected = (50 << 16) | (50 << 8) | 50;  // GRB format
    TEST_ASSERT_EQUAL_HEX(expected, result, "Color scaling 50%");

    result = hw_urgb_u32(255, 255, 255, 0.0f);
    TEST_ASSERT_EQUAL_HEX(0, result, "Brightness 0 should be black");
}

// ==========================================
// PART 2: COMPONENT TESTS (Hardware Drivers)
// ==========================================

void component_test_sd_storage()
{
    printf("\n[COMPONENT] Testing SD Card Storage...\n");
    FATFS fs;
    FIL fil;
    FRESULT fr;
    UINT bw;

    // 1. Driver Initialization
    if (!hw_sd_init(&fs)) {
        TEST_ASSERT(false, "SD Mount Failed (Check wiring)");
        return;
    }
    TEST_ASSERT(true, "SD Mount Success");

    // 2. Write File
    fr = f_open(&fil, "COMP_TEST.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        f_write(&fil, "TEST", 4, &bw);
        f_close(&fil);
        TEST_ASSERT(bw == 4, "File Write (4 bytes)");
    } else {
        TEST_ASSERT(false, "File Open Failed");
    }

    // 3. File Cleanup
    f_unlink("COMP_TEST.TXT");
}

void component_test_wifi_driver()
{
    printf("\n[COMPONENT] Testing Wi-Fi Driver...\n");

    // 1. Chip Init
    if (cyw43_arch_init()) {
        TEST_ASSERT(false, "Wi-Fi Chip Init Failed");
        return;
    }
    TEST_ASSERT(true, "Wi-Fi Chip Init Success");

    // 2. Connection Handshake
    cyw43_arch_enable_sta_mode();
    printf("   (Attempting connection to %s...)\n", TEST_WIFI_SSID);
    int err = cyw43_arch_wifi_connect_timeout_ms(
        TEST_WIFI_SSID, TEST_WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000);

    if (err == 0) {
        TEST_ASSERT(true, "Wi-Fi Connected (Valid IP)");
    } else {
        printf("   Error: %d\n", err);
        TEST_ASSERT(false, "Wi-Fi Connection Failed");
    }
    cyw43_arch_deinit();
}

// ==========================================
// MAIN RUNNER
// ==========================================
int main(void)
{
    stdio_init_all();
    sleep_ms(3000);  // Wait for USB Serial

    printf("\n\n========================================\n");
    printf("   PICO UNIT & COMPONENT TESTS (FULL)   \n");
    printf("========================================\n");

    // --- UNIT TESTS (Safe, Fast) ---
    unit_test_packet_msg_id();
    unit_test_token_matching();
    unit_test_block_size_math();
    unit_test_block2_encoding();  // Restored
    unit_test_reliability_basic();
    unit_test_reliability_circular_buffer();  // Restored
    unit_test_led_math();                     // Restored

    // --- COMPONENT TESTS (Hardware Dependent) ---
    component_test_sd_storage();
    component_test_wifi_driver();

    printf("\n----------------------------------\n");
    printf("SUMMARY: %d Tests Run\n", tests_total);
    if (tests_failed == 0) {
        printf("RESULT:  ALL PASSED! :)\n");
    } else {
        printf("RESULT:  %d FAILED :(\n", tests_failed);
    }
    printf("----------------------------------\n");

    while (1)
        sleep_ms(1000);
    return 0;
}
