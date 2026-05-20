/**
 * @file xbee.c
 * @brief XBee API frame builder and parser
 * @note Frames: - AT Command 0x08
 *               - Transmit Request 0x10
 *               - AT Response 0x88
 *               - TX Status 0x8B
 *               - RX Packet 0x90
 */

// Headers /////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <stdio.h>

#include "xbee.h"

// Defines /////////////////////////////////////////////////////////////////////////////

#define XBEE_API_START 0x7E
#define API_FRAME_HEADER_LENGTH 3 // Header: StartDelimiter | Length MSB | Length LSB

// Types ///////////////////////////////////////////////////////////////////////////////

typedef enum
{
    RX_STATE_WAIT_START,
    RX_STATE_READ_LEN,
    // RX_STATE_READ_FRAME,
} RxState_t;

// Local Variables /////////////////////////////////////////////////////////////////////

/* Buffer for building frames: Header | [Payload] | Checksum */
static uint8_t buf[3 + MAX_PAYLOAD_SIZE + 1];
static uint8_t frameIdCntr = 1; // Frame ID counter for tracking frames

// Tx Functions ////////////////////////////////////////////////////////////////////////

/**
 * @brief Calculates the checksum for a payload to ensure data integrity
 * @param[in] frame Pointer to the payload (starting after the header)
 * @param[in] len Length of the payload
 * @return uint8_t The calculated checksum value
 */
static uint8_t XBeeChecksum(const uint8_t *payload, uint16_t len)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++)
        sum += payload[i];
    return 0xFF - sum;
}

/**
 * @brief Builds an XBee API frame with the given payload length
 * @param[in] len Length of the payload to be included in the frame
 * @note The frame is built in a static buffer and includes the header and checksum. The
 *       caller should ensure that the payload is correctly placed (after header) before
 *       calling this function, and that a space is available for FrameID at Header + 1.
 */
static void XBeeBuildAPIFrame(size_t len)
{
    buf[0] = XBEE_API_START;
    buf[1] = ((uint16_t)len >> 8) & 0xFF;
    buf[2] = (uint16_t)len & 0xFF;
    buf[API_FRAME_HEADER_LENGTH + 1] = frameIdCntr++; // Increment ID for each frame
    if (frameIdCntr == 0)
        frameIdCntr = 1; // Avoid frame ID 0 which is reserved for no response
    buf[API_FRAME_HEADER_LENGTH + len] = XBeeChecksum(buf + API_FRAME_HEADER_LENGTH, len);
}

XBeeAPIStatus_t XBeeBuildAT(const char *atCmd, const uint8_t *param, uint8_t paramLen,
                            uint8_t **frame, uint8_t *frameID)
{
    /* Payload: 0x08 | FrameID | ATChar0 | ATChar1 | [ParamBytes] */
    XBeeAPIStatus_t status = XBEE_API_TX_SUCCESS;
    if (paramLen > (MAX_PAYLOAD_SIZE - 4))
        status = XBEE_API_TX_ERROR_FRAME_TOO_LARGE;
    else
    {
        size_t idx = 3;
        buf[idx++] = XBEE_API_TYPE_AT_COMMAND;
        idx++; // Leave space for FrameID
        buf[idx++] = atCmd[0];
        buf[idx++] = atCmd[1];
        if (param && paramLen)
        {
            memcpy(&buf[idx], param, paramLen);
            idx += paramLen;
        }
        XBeeBuildAPIFrame(idx);
        *frame = buf;
        *frameID = frameIdCntr;
    }

    return status;
}

XBeeAPIStatus_t XBeeBuildTxRequest(const uint8_t *dest64, const uint8_t *dest16, const uint8_t *rfData, size_t rfLen,
                                   uint8_t **frame, uint8_t *frameID)
{
    /* Payload: 0x10 | FrameID | 64-bit Dest | 16-bit Dest | BroadcastRadius | Options | [RF Data] */
    XBeeAPIStatus_t status = XBEE_API_TX_SUCCESS;
    if (rfLen > (MAX_PAYLOAD_SIZE - 14)) // 14 bytes for fixed fields
        status = XBEE_API_TX_ERROR_FRAME_TOO_LARGE;
    else
    {
        size_t idx = 3;
        buf[idx++] = XBEE_API_TYPE_TX_REQUEST;
        idx++; // Leave space for FrameID
        memcpy(&buf[idx], dest64, 8);
        idx += 8;
        memcpy(&buf[idx], dest16, 2);
        idx += 2;
        buf[idx++] = 0x00; // Broadcast radius
        buf[idx++] = 0x00; // Options
        if (rfLen && rfData)
        {
            memcpy(&buf[idx], rfData, rfLen);
            idx += rfLen;
        }
        XBeeBuildAPIFrame(idx);
        *frame = buf;
        *frameID = frameIdCntr;
    }

    return status;
}

// Rx Functions ////////////////////////////////////////////////////////////////////////

uint16_t XBeeRxLen(const uint8_t c)
{
    static uint8_t lenIdx = 0, lenBuf[2];
    static RxState_t rxState = RX_STATE_WAIT_START;
    uint16_t len = 0;

    /* Receive frame length */
    switch (rxState)
    {
    case RX_STATE_WAIT_START: // Wait for start delimiter
        if (c == XBEE_API_START)
            rxState = RX_STATE_READ_LEN;
        break;
    case RX_STATE_READ_LEN: // Read length bytes
        lenBuf[lenIdx++] = c;

        if (lenIdx == 2)
        {
            lenIdx = 0;
            len = (lenBuf[0] << 8) | lenBuf[1];
            rxState = RX_STATE_WAIT_START;
        }
        break;
    default:
        break;
    }

    return len;
}

XBeeAPIStatus_t XBeeParseFrame(const uint8_t *payload, size_t len,
                               XBeeAPIParsedFrame_t *parsedFrame)
{
    uint8_t frameType = payload[0], checksum = payload[len - 1];
    XBeeAPIStatus_t status = XBEE_API_RX_SUCCESS;

    /* Validate checksum */
    uint8_t calc = XBeeChecksum(payload, len);
    if (calc != checksum)
        status = XBEE_API_RX_ERROR_INVALID_CHECKSUM;
    else
    {
        /* Process frame types of interest */
        switch (frameType)
        {
        case XBEE_API_TYPE_AT_RESPONSE:
            /* Frame: 0x88 | FrameID | ATChar0 | ATChar1 | [ParamBytes] */
            parsedFrame->type = frameType;
            parsedFrame->atResponse.frameId = payload[1];
            parsedFrame->atResponse.atCmd[0] = payload[2];
            parsedFrame->atResponse.atCmd[1] = payload[3];
            parsedFrame->atResponse.param = &payload[4];
            parsedFrame->atResponse.paramLen = len - (1 + 1 + 1 + 1 + 1);
            break;
        case XBEE_API_TYPE_TX_STATUS:
            /* Frame: 0x8B | FrameID | 64-bit dest | 16-bit dest | TransmitRetryCount | DeliveryStatus | DiscoveryStatus */
            parsedFrame->type = frameType;
            parsedFrame->txStatus.frameId = payload[1];
            parsedFrame->txStatus.deliveryStatus = payload[(1 + 8 + 2 + 1)];
            break;
        case XBEE_API_TYPE_RX_PACKET:
            /* Frame: 0x90 | 64-bit src | 16-bit src | RSSI | Options | [RF Data] */
            parsedFrame->type = frameType;
            parsedFrame->rxPacket.rssi = payload[(1 + 8 + 2)];
            parsedFrame->rxPacket.rfData = &payload[1 + 8 + 2 + 1 + 1];
            parsedFrame->rxPacket.rfDataLen = len - (1 + 8 + 2 + 1 + 1 + 1);
            break;
        default:
            break;
        }
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////////////
