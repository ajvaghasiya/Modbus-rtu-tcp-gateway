/**
 * test_crc.c - Unit tests for CRC16 calculation
 *
 * Tests against known Modbus RTU frame CRC values.
 * Build: make test
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "modbus_rtu.h"

#define PASS "\033[32mPASS\033[0m"
#define FAIL "\033[31mFAIL\033[0m"

static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(desc, expr) do { \
    tests_run++; \
    if (expr) { printf("  [" PASS "] %s\n", desc); tests_pass++; } \
    else       { printf("  [" FAIL "] %s\n", desc); } \
} while(0)

/* Known-good test vectors from Modbus specification */
static void test_known_vectors(void) {
    printf("\n--- CRC16 Known Vectors ---\n");

    /* FC03 request: slave=1, FC=3, addr=0x006B, count=0x0003 -> CRC=0x7687 */
    uint8_t req1[] = {0x01, 0x03, 0x00, 0x6B, 0x00, 0x03};
    uint16_t crc1 = rtu_crc16(req1, sizeof(req1));
    CHECK("FC03 request CRC = 0x7687", crc1 == 0x7687);

    /* FC01 request: slave=1, FC=1, addr=0x0013, count=0x0025 -> CRC=0x0DCE */
    uint8_t req2[] = {0x01, 0x01, 0x00, 0x13, 0x00, 0x25};
    uint16_t crc2 = rtu_crc16(req2, sizeof(req2));
    CHECK("FC01 request CRC = 0x0DCE", crc2 == 0x0DCE);

    /* Single byte 0x01 -> CRC=0x807E */
    uint8_t req3[] = {0x01};
    uint16_t crc3 = rtu_crc16(req3, 1);
    CHECK("Single byte 0x01 CRC = 0x807E", crc3 == 0x807E);

    /* Empty data -> CRC = 0xFFFF */
    uint16_t crc4 = rtu_crc16(NULL, 0);
    CHECK("Empty data CRC = 0xFFFF", crc4 == 0xFFFF);
}

static void test_frame_build_validate(void) {
    printf("\n--- Frame Build & Validate ---\n");

    uint8_t pdu[]  = {0x03, 0x00, 0x00, 0x00, 0x08};
    uint8_t frame[32];
    int len = rtu_build_frame(1, pdu, sizeof(pdu), frame);

    CHECK("Frame length = pdu + 3", len == (int)(sizeof(pdu) + 3));
    CHECK("First byte is slave ID", frame[0] == 1);
    CHECK("Second byte is FC", frame[1] == 0x03);
    CHECK("Frame validates correctly", rtu_validate_frame(frame, len) == 0);

    /* Corrupt CRC and check detection */
    frame[len-1] ^= 0xFF;
    CHECK("Corrupted CRC detected", rtu_validate_frame(frame, len) != 0);

    /* Short frame */
    CHECK("Short frame (3 bytes) rejected", rtu_validate_frame(frame, 3) != 0);
}

static void test_crc_appended_correctly(void) {
    printf("\n--- CRC Byte Order in Frame ---\n");
    uint8_t data[] = {0x01, 0x03, 0x00, 0x6B, 0x00, 0x03};
    uint8_t frame[16];
    int len = rtu_build_frame(data[0], data+1, 5, frame);
    /* CRC = 0x7687: low byte = 0x87, high byte = 0x76 */
    CHECK("CRC low byte appended first",  frame[len-2] == 0x87);
    CHECK("CRC high byte appended second", frame[len-1] == 0x76);
}

int main(void) {
    printf("=== Modbus RTU CRC16 Unit Tests ===\n");
    test_known_vectors();
    test_frame_build_validate();
    test_crc_appended_correctly();
    printf("\n=== Results: %d/%d passed ===\n", tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
