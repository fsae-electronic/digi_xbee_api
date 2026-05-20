#include "xbee.h"

#define XBEE_API_START 0x7E
#define RX_BUF_SIZE 512
static uint8_t rx_buf[RX_BUF_SIZE];
static size_t rx_idx = 0;

/* Read and parse incoming API frames */
void xbee_rx(uint8_t c)
{
    uint16_t len;

    if (rx_idx < RX_BUF_SIZE)
        rx_buf[rx_idx++] = c;

    /* Try to parse complete frames from buffer */
    while (rx_idx >= 3)
    {
        if (rx_buf[0] != XBEE_API_START)
        {
            /* Shift until start */
            memmove(rx_buf, rx_buf + 1, --rx_idx);
            continue;
        }

        len = ((uint16_t)rx_buf[1] << 8) | rx_buf[2];
        if (len > (RX_BUF_SIZE - 4))
        {
            printf("XBee frame too long: %u bytes\n", len);
            /* Reset state */
            memmove(rx_buf, rx_buf + 3, rx_idx - 3);
            rx_idx -= 3;
            continue;
        }
        if (rx_idx < 3 + len + 1)
            break; // Wait more

        /* Full frame available */
        uint8_t frame_type = rx_buf[3];
        uint8_t *frame_payload = &rx_buf[3];
        size_t frame_len = len;
        uint8_t checksum = rx_buf[3 + len];

        /* Validate checksum */
        uint8_t calc = xbee_checksum(frame_payload, frame_len);
        if (calc != checksum)
        {
            printk("XBee frame checksum fail\n");
        }
        else
        {
            /* Process frame types of interest: TX_STATUS (0x8B), RX (0x90) */
            if (frame_type == 0x8B)
            {
                /* TX Status: parse frame */
                /* Frame structure: 0x8B | FrameID | 64-bit dest | 16-bit dest | TransmitRetryCount | DeliveryStatus | DiscoveryStatus */
                uint8_t frame_id = frame_payload[1];
                uint8_t delivery_status = frame_payload[(1 + 8 + 2 + 1)]; /* index */
                printk("TX status frame_id=%u delivery=%02x\n", frame_id, delivery_status);
            }
            else if (frame_type == 0x90)
            {
                /* RX packet: 0x90 | 64-bit src | 16-bit src | RSSI | Options | RF Data... */
                uint8_t rssi = frame_payload[(1 + 8 + 2)];
                size_t rf_offset = 1 + 8 + 2 + 1 + 1;
                size_t rf_len_local = frame_len - rf_offset;
                printk("RX from remote, rssi=%u, len=%u\n", rssi, (unsigned)rf_len_local);
                /* Dump payload */
                for (size_t i = 0; i < rf_len_local && i < 64; ++i)
                {
                    printk("%02x ", frame_payload[rf_offset + i]);
                }
                printk("\n");
            }
            else
            {
                printk("XBee frame type: %02x len=%u\n", frame_type, (unsigned)frame_len);
            }
        }

        /* Remove this frame from buffer */
        size_t remove_len = 3 + len + 1;
        memmove(rx_buf, rx_buf + remove_len, rx_idx - remove_len);
        rx_idx -= remove_len;
    }
}