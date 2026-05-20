/**
 * @file test.c
 * @brief Unit tests for XBee packet creation and parsing
 * @note Tests packet format, checksums, and frame parsing without UART I/O
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "xbee.h"

/* =================================== TEST UTILS =================================== */

/* Mock UART TX buffer for capturing sent frames */
#define TX_BUF_SIZE 1024
static uint8_t tx_buf[TX_BUF_SIZE];
static size_t tx_idx = 0;

/* Mock UART send function */
static void mock_uart_send_byte(uint8_t byte)
{
    if (tx_idx < TX_BUF_SIZE)
    {
        tx_buf[tx_idx++] = byte;
    }
}

/* Reset TX buffer */
static void reset_tx_buffer(void)
{
    tx_idx = 0;
    memset(tx_buf, 0, TX_BUF_SIZE);
}

/* Get buffer content */
static const uint8_t *get_tx_buffer(size_t *out_len)
{
    *out_len = tx_idx;
    return tx_buf;
}

/* Print hex dump of buffer */
static void hex_dump(const uint8_t *buf, size_t len, const char *label)
{
    printf("  %s: ", label);
    for (size_t i = 0; i < len; ++i)
    {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

/* ============================ ADAPTABLE XBEE FUNCTIONS ============================ */

/* Send raw bytes on mock UART */
static void uart_send_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        mock_uart_send_byte(buf[i]);
    }
}

/* ================================= FRAME PARSING ================================== */

typedef struct
{
    uint8_t frame_type;
    uint8_t *frame_payload;
    size_t frame_len;
    bool valid;
} parsed_frame_t;

/* Parse a single frame from buffer (assumes complete frame at start) */
static parsed_frame_t parse_frame(const uint8_t *buf, size_t buf_len)
{
    parsed_frame_t result = {0};

    if (buf_len < 4)
    { /* min: start + len(2) + frame_type + checksum */
        result.valid = false;
        return result;
    }

    if (buf[0] != 0x7E)
    {
        result.valid = false;
        return result;
    }

    uint16_t len = ((uint16_t)buf[1] << 8) | buf[2];

    if (buf_len < 3 + len + 1)
    {
        result.valid = false;
        return result;
    }

    result.frame_type = buf[3];
    result.frame_payload = (uint8_t *)&buf[3];
    result.frame_len = len;

    uint8_t checksum = buf[3 + len];
    uint8_t calc = xbee_checksum(result.frame_payload, result.frame_len);

    if (calc != checksum)
    {
        result.valid = false;
        printf("    Checksum mismatch: calculated=%02X, received=%02X\n", calc, checksum);
        return result;
    }

    result.valid = true;
    return result;
}

/* =================================== TEST CASES =================================== */

static int test_count_main = 0;
static int test_count = 0;
static int test_passed = 0;

void test_start(const char *name)
{
    test_count_main++;
    printf("\n[TEST %d] %s\n", test_count_main, name);
}

void test_assert(bool condition, const char *msg)
{
    test_count++;
    if (condition)
    {
        printf("  - %s\n", msg);
        test_passed++;
    }
    else
    {
        printf("  + %s\n", msg);
    }
}

/* Test 1: Checksum calculation */
void test_checksum(void)
{
    test_start("Checksum Calculation");

    uint8_t payload1[] = {0x08, 0x01, 'I', 'D', 0x12, 0x34};
    uint8_t expected1 = 0xFF - (0x08 + 0x01 + 'I' + 'D' + 0x12 + 0x34);
    uint8_t actual1 = xbee_checksum(payload1, sizeof(payload1));

    printf("  Payload: ");
    for (size_t i = 0; i < sizeof(payload1); ++i)
        printf("%02X ", payload1[i]);
    printf("\n");
    printf("  Expected checksum: %02X, Calculated: %02X\n", expected1, actual1);
    test_assert(actual1 == expected1, "Simple checksum matches");

    /* Test with empty payload */
    uint8_t empty[1] = {0};
    uint8_t expected_empty = 0xFF;
    uint8_t actual_empty = xbee_checksum(empty, 0);
    test_assert(actual_empty == expected_empty, "Empty payload checksum = 0xFF");
}

/* Test 2: AT Command packet creation */
void test_at_command_frame(void)
{
    test_start("AT Command Frame Creation");

    reset_tx_buffer();

    /* Send AT ID 0x12 0x34 */
    uint8_t params[] = {0x12, 0x34};
    xbee_send_at("ID", params, 2, 0x01);

    size_t tx_len;
    const uint8_t *tx_data = get_tx_buffer(&tx_len);
    hex_dump(tx_data, tx_len, "TX Buffer");

    /* Expected structure:
       [0x7E] [0x00 0x06] [0x08 0x01 0x49 0x44 0x12 0x34] [checksum]
       Start(1) + Len(2) + Payload(6) + Checksum(1) = 10 bytes
    */

    test_assert(tx_len == 10, "Frame length is 10 bytes");
    test_assert(tx_data[0] == 0x7E, "Start byte is 0x7E");
    test_assert(tx_data[1] == 0x00 && tx_data[2] == 0x06, "Length field = 0x0006");
    test_assert(tx_data[3] == 0x08, "Frame type is 0x08 (AT Command)");
    test_assert(tx_data[4] == 0x01, "Frame ID is 0x01");
    test_assert(tx_data[5] == 'I' && tx_data[6] == 'D', "AT command is 'ID'");
    test_assert(tx_data[7] == 0x12 && tx_data[8] == 0x34, "Parameters are 0x12 0x34");

    /* Verify checksum */
    uint8_t expected_csum = xbee_checksum(&tx_data[3], 6);
    test_assert(tx_data[9] == expected_csum, "Checksum is valid");
}

/* Test 3: Transmit Request packet creation */
void test_transmit_request_frame(void)
{
    test_start("Transmit Request Frame Creation");

    reset_tx_buffer();

    uint8_t dest64[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    uint8_t dest16[] = {0xFF, 0xFE};
    uint8_t rf_data[] = {0xAA, 0x01, 0x02, 0x03};

    xbee_send_transmit_request(0x05, dest64, dest16, rf_data, sizeof(rf_data));

    size_t tx_len;
    const uint8_t *tx_data = get_tx_buffer(&tx_len);
    hex_dump(tx_data, tx_len, "TX Buffer");

    /* Expected structure:
       [0x7E] [Len MSB] [Len LSB] [Payload...] [Checksum]
       Payload: 0x10 + FrameID(1) + Dest64(8) + Dest16(2) + BroadcastRad(1) + Options(1) + RFData(4)
                = 1 + 1 + 8 + 2 + 1 + 1 + 4 = 18 bytes
       Total = 1 + 2 + 18 + 1 = 22 bytes
    */

    test_assert(tx_len == 22, "Frame length is 22 bytes");
    test_assert(tx_data[0] == 0x7E, "Start byte is 0x7E");
    test_assert(tx_data[1] == 0x00 && tx_data[2] == 0x12, "Length field = 0x0012");
    test_assert(tx_data[3] == 0x10, "Frame type is 0x10 (Transmit Request)");
    test_assert(tx_data[4] == 0x05, "Frame ID is 0x05");

    /* Check destination addresses */
    test_assert(memcmp(&tx_data[5], dest64, 8) == 0, "64-bit destination matches");
    test_assert(memcmp(&tx_data[13], dest16, 2) == 0, "16-bit destination matches");
    test_assert(tx_data[15] == 0x00, "Broadcast radius is 0x00");
    test_assert(tx_data[16] == 0x00, "Options is 0x00");
    test_assert(memcmp(&tx_data[17], rf_data, 4) == 0, "RF data matches");

    /* Verify checksum */
    uint8_t expected_csum = xbee_checksum(&tx_data[3], 18);
    test_assert(tx_data[21] == expected_csum, "Checksum is valid");
}

/* Test 4: Frame parsing */
void test_frame_parsing(void)
{
    test_start("Frame Parsing");

    /* Create and parse an AT command frame */
    reset_tx_buffer();
    uint8_t params[] = {0x12, 0x34};
    xbee_send_at("ID", params, 2, 0x01);

    size_t tx_len;
    const uint8_t *tx_data = get_tx_buffer(&tx_len);

    parsed_frame_t frame = parse_frame(tx_data, tx_len);

    test_assert(frame.valid, "Frame parses successfully");
    test_assert(frame.frame_type == 0x08, "Frame type is 0x08");
    test_assert(frame.frame_len == 6, "Frame length is 6");
    test_assert(frame.frame_payload[1] == 0x01, "Frame ID is 0x01");
    test_assert(frame.frame_payload[2] == 'I' && frame.frame_payload[3] == 'D', "AT command parsed");
}

/* Test 5: Incomplete frame handling */
void test_incomplete_frame(void)
{
    test_start("Incomplete Frame Handling");

    reset_tx_buffer();
    uint8_t params[] = {0x12, 0x34};
    xbee_send_at("ID", params, 2, 0x01);

    size_t tx_len;
    const uint8_t *tx_data = get_tx_buffer(&tx_len);

    /* Try to parse with truncated buffer */
    parsed_frame_t frame = parse_frame(tx_data, 5); /* Too short */
    test_assert(!frame.valid, "Truncated frame is rejected");

    /* Parse with minimum valid size */
    frame = parse_frame(tx_data, tx_len);
    test_assert(frame.valid, "Complete frame parses correctly");
}

/* Test 6: RX packet parsing simulation */
void test_rx_packet_parsing(void)
{
    test_start("RX Packet Parsing Simulation");

    /* Manually construct an RX packet: 0x90 | 64-bit src | 16-bit src | RSSI | Options | RF Data */
    uint8_t rx_packet[] = {
        0x7E,                                           /* Start */
        0x00, 0x0F,                                     /* Length: 15 bytes */
        0x90,                                           /* Frame type: RX Packet */
        0x00, 0x13, 0xA2, 0x00, 0x41, 0x68, 0x22, 0x11, /* 64-bit source */
        0xFF, 0xFE,                                     /* 16-bit source */
        0x50,                                           /* RSSI: -80 dBm */
        0x01,                                           /* Options */
        0xAA, 0xBB,                                     /* RF Data */
        0x00                                            /* Placeholder for checksum, will be calculated */
    };

    /* Calculate and add checksum */
    uint8_t payload_start = 3;
    uint8_t payload_len = 15;
    uint8_t csum = xbee_checksum(&rx_packet[payload_start], payload_len);
    rx_packet[payload_start + payload_len] = csum;

    parsed_frame_t frame = parse_frame(rx_packet, sizeof(rx_packet));

    test_assert(frame.valid, "RX packet parses successfully");
    test_assert(frame.frame_type == 0x90, "Frame type is 0x90 (RX)");
    test_assert(frame.frame_payload[1 + 8 + 2] == 0x50, "RSSI field is 0x50");
    test_assert(frame.frame_payload[1 + 8 + 2 + 1] == 0x01, "Options field is 0x01");

    /* Extract RF data offset and length */
    size_t rf_offset = 1 + 8 + 2 + 1 + 1;
    size_t rf_len = frame.frame_len - rf_offset;
    test_assert(rf_len == 2, "RF data length is 2 bytes");
    test_assert(frame.frame_payload[rf_offset] == 0xAA && frame.frame_payload[rf_offset + 1] == 0xBB,
                "RF data matches");
}

/* ================================ MAIN TEST RUNNER ================================ */

int main(void)
{
    printf("================================\n");
    printf("  XBee Packet Testing Suite     \n");
    printf("================================\n");

    test_checksum();
    test_at_command_frame();
    test_transmit_request_frame();
    test_frame_parsing();
    test_incomplete_frame();
    test_rx_packet_parsing();

    printf("\n================================\n");
    printf("   Test Results: %d/%d passed   \n", test_passed, test_count);
    printf("================================\n");

    return (test_passed == test_count) ? 0 : 1;
}
