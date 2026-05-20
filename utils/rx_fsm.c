#include "xbee.h"

#define XBEE_API_START 0x7E
#define RX_BUF_SIZE 256

typedef enum
{
    RX_STATE_WAIT_START,
    RX_STATE_READING_LEN,
    RX_STATE_READING_DATA,
} rx_state_t;

typedef struct
{
    uint8_t type;
    uint16_t length;
    uint8_t checksum;
    uint8_t data[RX_BUF_SIZE];
} xbee_api_frame_t;

/* Parser for incoming API frames */
XBeeAPIStatus_t xbee_rx(const uint8_t c, uint8_t **frame_data, uint16_t *frame_length)
{
    static xbee_api_frame_t frame;
    static size_t rx_idx = 0;
    static rx_state_t rx_state = RX_STATE_WAIT_START;
    XBeeAPIStatus_t result;

    /* State machine for parsing API frames */
    /* Frame: 0x7E | len MSB | len LSB | [frame data] | checksum */

    switch (rx_state)
    {
    case RX_STATE_WAIT_START:
        /* Wait for start delimiter */
        if (c == XBEE_API_START)
        {
            rx_idx = 0;
            rx_state = RX_STATE_READING_LEN;
            result = XBEE_API_RX_READING;
        }
        else
            result = XBEE_API_RX_WAITING_START;
        break;

    case RX_STATE_READING_LEN:
        /* Read length bytes */
        if (rx_idx++ < 2)
            frame.length = (frame.length << 8) | c;
        else // Length received
        {
            /* Check if frame is too long */
            if (frame.length > RX_BUF_SIZE)
            {
                printk("XBee frame too long: %u bytes\n", frame.length);
                rx_state = RX_STATE_WAIT_START; // Reset state
                result = XBEE_API_RX_ERROR_FRAME_TOO_LARGE;
            }
            else
            {
                rx_idx = 0;
                rx_state = RX_STATE_READING_DATA;
                result = XBEE_API_RX_READING;
            }
        }
        break;

    case RX_STATE_READING_DATA:
        /* Store incoming byte */
        frame.data[rx_idx++] = c;

        /* Accumulate bytes in buffer until the frame is complete */
        if (rx_idx == frame.length)
        {
            /* Full frame available */
            frame.type = frame.data[3];
            uint8_t *frame_payload = &frame.data[3];
            frame.checksum = frame.data[3 + frame.length];

            /* Validate checksum */
            uint8_t calc = XBeeChecksum(frame_payload, frame.length);
            if (calc != frame.checksum)
            {
                printk("XBee frame checksum fail\n");
                rx_state = RX_STATE_WAIT_START; // Reset state
                result = XBEE_API_RX_ERROR_INVALID_CHECKSUM;
            }
            else
            {
                rx_state = RX_STATE_WAIT_START; // Reset state for next frame
                result = XBEE_API_RX_SUCCESS;

                /* Process frame types of interest: TX_STATUS (0x8B), RX (0x90) */
                if (frame.type == 0x8B) // TX Status
                {
                    /* Frame: 0x8B | FrameID | 64-bit dest | 16-bit dest | TransmitRetryCount | DeliveryStatus | DiscoveryStatus */
                    uint8_t frame_id = frame_payload[1];
                    uint8_t delivery_status = frame_payload[(1 + 8 + 2 + 1)];
                    *frame_data = &delivery_status;
                    *frame_length = 1;

                    printk("TX status frame_id=%u delivery=%02x\n", frame_id, delivery_status);
                }
                else if (frame.type == 0x90) // RX Packet
                {
                    /* Frame: 0x90 | 64-bit src | 16-bit src | RSSI | Options | [RF Data] */
                    uint8_t rssi = frame_payload[(1 + 8 + 2)];
                    size_t rf_offset = 1 + 8 + 2 + 1 + 1;
                    size_t rf_len_local = frame.length - rf_offset;
                    *frame_data = frame_payload + rf_offset;
                    *frame_length = rf_len_local;

                    printk("RX from remote, rssi=%u, len=%u\n", rssi, (unsigned)rf_len_local);
                    /* Dump payload */
                    for (size_t i = 0; i < rf_len_local && i < 64; ++i)
                        printf("%02x ", frame_payload[rf_offset + i]);
                    printf("\n");
                }
                else
                {
                    printk("XBee frame type: %02x len=%u\n", frame.type, (unsigned)frame.length);
                    result = XBEE_API_RX_ERROR_INVALID_FRAME;
                }
            }
        }
    }

    return result;
}