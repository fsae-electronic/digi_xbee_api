#include "sci.h"
#include "xbee.h"

/* Example PAN command (ID param) */
static const uint8_t pan_param[2] = {0x12, 0x34};

/* Example destinations: broadcast */
static const uint8_t dest_64[8] = {0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
static const uint8_t dest_16[2] = {0xFF, 0xFE};

/* Example payload */
static const uint8_t data[] = {0xAA, 0x01, 0x02, 0x03};

int main(void)
{
    uint8_t c;

    /* Send initial AT command to set PAN ID */
    xbee_send_at("ID", pan_param, 2, 0x01);
    /* AT Response (0x88) response is asynchronous; parse in main loop */

    while (1)
    {
        /* Try to read and parse incoming frames from UART */
        while (sciIsRxReady(sciREG)) // Check if data is available
        {
            c = sciReceiveByte(sciREG); // Read byte from UART
            xbee_rx(c);                 // Feed byte to XBee parser
        }

        /* Periodically send telemetry */
        xbee_tx(dest_64, dest_16, data, sizeof(data));
        printf("Telemetry packet sent\n");

        /* Delay loop */
        for (volatile int i = 0; i < 1000000; i++)
            ;
    }

    return 0;
}
