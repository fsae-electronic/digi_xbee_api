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

// #include <string.h>

#include "xbee.h"

// Defines /////////////////////////////////////////////////////////////////////////////

#define XBEE_API_START (0x7EU)
// #define XBEE_API_DATA_HEAD_ID_SZ (1U) // FrameID

#define XBEE_API_FUNC(func, frame) XBeeAPIStatus_t func(XBeeAPIFrame_t *frame)

// Types ///////////////////////////////////////////////////////////////////////////////

typedef enum
{
    RX_STATE_START,
    RX_STATE_LEN_MSB,
    RX_STATE_LEN_LSB,
    RX_STATE_DATA
    // RX_STATE_CHECKSUM
} RxState_t;

// typedef struct
// {
//     XBeeAPITxStatusFrame_t txStatus;
//     uint8_t isActive;
// } XBeeAPITxTracker_t;

// Local Variables /////////////////////////////////////////////////////////////////////

// XBeeAPITxTracker_t txTable[XBEE_MAX_PENDING_FRAMES];

// Prototypes //////////////////////////////////////////////////////////////////////////

/**
 * @brief Builders and parsers for specific XBee API frame types
 * @param[in] frame Pointer to the XBeeAPIFrame_t structure to be built or parsed
 * @return XBeeAPIStatus_t The status of the operation
 */
static XBEE_API_FUNC_PROTO(XBeeAPIBuildATCommand, frame);
static XBEE_API_FUNC_PROTO(XBeeAPIBuildTxRequest, frame);
static XBEE_API_FUNC_PROTO(XBeeAPIParseATResponse, frame);
static XBEE_API_FUNC_PROTO(XBeeAPIParseTxStatus, frame);
static XBEE_API_FUNC_PROTO(XBeeAPIParseRxPacket, frame);

/**
 * @brief Calculates the checksum for a payload to ensure data integrity
 * @param[in] frame Pointer to the data (starting after the frame header)
 * @param[in] len Length of the data
 * @return uint8_t The calculated checksum value
 */
static uint8_t XBeeChecksum(const uint8_t *payload, uint16_t len);

// Tx Functions ////////////////////////////////////////////////////////////////////////

XBEE_API_FUNC(XBeeAPIBuildFrame, frame)
{
    XBeeAPIStatus_t status;
    uint8_t csum;

    switch (frame->type)
    {
    case XBEE_API_TYPE_AT_COMMAND:
        status = XBeeBuildAT(frame);
        break;
    case XBEE_API_TYPE_TX_REQUEST:
        status = XBeeBuildTxRequest(frame);
        break;
    }

    if (status == XBEE_API_TX_SUCCESS)
    {
        frame->buffer[0] = XBEE_API_START;
        memcpy(&frame->buffer[1], &frame->length, sizeof(frame->length));
        csum = XBeeChecksum(&frame->buffer[XBEE_API_FRAME_HEAD_SZ], frame->length);
        frame->length += XBEE_API_FRAME_HEAD_SZ + XBEE_API_CHECKSUM_SZ;
        frame->buffer[frame->length - 1] = csum;
    }

    return status;
}

static XBEE_API_FUNC(XBeeAPIBuildATCommand, frame)
{
    /* Payload: 0x08 | FrameID | 16-bit ATCmd | [Param] */
    XBeeAPIATCommandFrame_t *ATCmd = &frame->data.ATCommand;
    XBeeAPIStatus_t status = XBEE_API_TX_SUCCESS;
    uint8_t *bufferPtr = &frame->buffer[XBEE_API_FRAME_HEAD_SZ];

    if (ATCmd->paramLen > (XBEE_API_MAX_PAYLOAD_SZ))
        status = XBEE_API_TX_ERROR_FRAME_TOO_LARGE;
    else
    {
        /* Copy the data first in case the buffer was not used properly */
        if (ATCmd->param != bufferPtr + XBEE_API_DATA_HEAD_SZ_AT_COMMAND)
            memmove(bufferPtr + XBEE_API_DATA_HEAD_SZ_AT_COMMAND, ATCmd->param, ATCmd->paramLen);
        *bufferPtr = XBEE_API_TYPE_AT_COMMAND;
        memcpy(bufferPtr + 2, ATCmd->ATCmd, sizeof(ATCmd->ATCmd));
        frame->length = XBEE_API_DATA_HEAD_SZ_AT_COMMAND + ATCmd->paramLen;
    }

    return status;
}

static XBEE_API_FUNC(XBeeAPIBuildTxRequest, frame)
{
    /* Payload: 0x10 | FrameID | 64-bit Dest | 16-bit Dest | BroadcastRadius | TxOptions | [PayloadData] */
    XBeeAPITxRequestFrame_t *txReq = &frame->data.txRequest;
    XBeeAPIStatus_t status = XBEE_API_TX_SUCCESS;
    uint8_t *bufferPtr = &frame->buffer[XBEE_API_FRAME_HEAD_SZ];

    if (txReq->payloadDataLen > (XBEE_API_MAX_PAYLOAD_SZ))
        status = XBEE_API_TX_ERROR_FRAME_TOO_LARGE;
    else
    {
        /* Copy the data first in case the buffer was not used properly */
        if (txReq->payloadData != bufferPtr + XBEE_API_DATA_HEAD_SZ_TX_REQUEST)
            memmove(bufferPtr + XBEE_API_DATA_HEAD_SZ_TX_REQUEST, txReq->payloadData, txReq->payloadDataLen);
        *bufferPtr = XBEE_API_TYPE_TX_REQUEST;
        bufferPtr += sizeof(frame->type) + sizeof(txReq->frameID);
        memcpy(bufferPtr, &txReq->dest64, sizeof(txReq->dest64));
        bufferPtr += sizeof(txReq->dest64);
        memcpy(bufferPtr, &txReq->dest16, sizeof(txReq->dest16));
        bufferPtr += sizeof(txReq->dest16);
        *(bufferPtr++) = txReq->broadcastRadius;
        *(bufferPtr++) = txReq->txOptions;
        frame->length = XBEE_API_DATA_HEAD_SZ_TX_REQUEST + txReq->payloadDataLen;
    }

    return status;
}

// Rx Functions ////////////////////////////////////////////////////////////////////////

XBEE_API_FUNC(XBeeAPIParseFrame, frame)
{
    // XBeeAPIRxPacketFrame_t *rxPacket;
    XBeeAPIStatus_t status = XBEE_API_RX_SUCCESS;
    static RxState_t rxState = RX_STATE_START;
    static uint16_t len;

    /* Receive frame length */
    switch (rxState)
    {
    case RX_STATE_START:   // Wait for start delimiter
        frame->length = 0; // Reset frame length for new reception
        if (frame->buffer[0] == XBEE_API_START)
            rxState = RX_STATE_LEN_MSB;
        break;
    case RX_STATE_LEN_MSB: // Read length bytes
        len = ((uint16_t)frame->buffer[0] << 8);
        rxState = RX_STATE_LEN_LSB;
        break;
    case RX_STATE_LEN_LSB:
        len |= frame->buffer[0];
        rxState = RX_STATE_START;
        if (len == 0)
            status = XBEE_API_RX_ERROR_FRAME_TOO_LARGE;
        else if (len > XBEE_API_MAX_DATA_SZ)
            status = XBEE_API_RX_ERROR_INVALID_FRAME;
        else
        {
            frame->length = len + XBEE_API_CHECKSUM_SZ;
            rxState = RX_STATE_DATA;
            // rxState = RX_STATE_CHECKSUM;
        }
        break;
    case RX_STATE_DATA:
        /* Validate checksum before processing data */
        if (XBeeChecksum(frame->buffer, frame->length - 1) != frame->buffer[frame->length - 1])
        {
            status = XBEE_API_RX_ERROR_INVALID_CHECKSUM;
            rxState = RX_STATE_START; // Reset for next frame
        }
        else
        {
            /* Process frame types of interest */
            frame->type = (XBeeAPIFrameType_t)frame->buffer[0];
            switch (frame->type)
            {
            case XBEE_API_TYPE_AT_RESPONSE:
                XBeeAPIParseATResponse(frame);
                break;
            case XBEE_API_TYPE_TX_STATUS:
                XBeeAPIParseTxStatus(frame);
                break;
            case XBEE_API_TYPE_RX_PACKET:
                XBeeAPIParseRxPacket(frame);
                break;
            default:
                status = XBEE_API_RX_ERROR_INVALID_FRAME;
                break;
            }
        }
        break;
    // case RX_STATE_CHECKSUM:
    //     break;
    default:
        break;
    }

    return status;
}

static XBEE_API_FUNC(XBeeAPIParseATResponse, frame)
{
    /* Frame: 0x88 | FrameID | 16-bit ATcmd | CmdStatus | [CmdData] */
    XBeeAPIATResponseFrame_t *ATResponse = &frame->data.ATResponse;
    XBeeAPIStatus_t status = XBEE_API_RX_SUCCESS;

    ATResponse->frameID = frame->buffer[1];
    ATResponse->ATCmd = (uint16_t)((frame->buffer[2] << 8) | frame->buffer[3]);
    ATResponse->cmdStatus = (XBeeAPICommandStatus_t)frame->buffer[4];
    ATResponse->cmdData = (uint8_t *)&frame->buffer[5];
    ATResponse->cmdDataLen = frame->length - 5;

    return status;
}

static XBEE_API_FUNC(XBeeAPIParseTxStatus, frame)
{
    /* Frame: 0x8B | FrameID | DeliveryStatus */
    XBeeAPITxStatusFrame_t *txStatus = &frame->data.txStatus;
    XBeeAPIStatus_t status = XBEE_API_RX_SUCCESS;

    txStatus->frameID = frame->buffer[1];
    txStatus->deliveryStatus = frame->buffer[2];

    return status;
}

static XBEE_API_FUNC(XBeeAPIParseRxPacket, frame)
{
    /* Frame: 0x90 | 64-bit src | 16-bit src | RxOptions | [RF Data] */
    XBeeAPIRxPacketFrame_t *rxPacket = &frame->data.rxPacket;
    XBeeAPIStatus_t status = XBEE_API_RX_SUCCESS;

    memcpy(&rxPacket->src64, &frame->buffer[1], sizeof(rxPacket->src64));
    memcpy(&rxPacket->src16, &frame->buffer[9], sizeof(rxPacket->src16));
    rxPacket->rxOptions = frame->buffer[11];
    rxPacket->rxData = &frame->buffer[12];
    rxPacket->rxDataLen = frame->length - 12;

    return status;
}

////////////////////////////////////////////////////////////////////////////////////////

static uint8_t XBeeChecksum(const uint8_t *payload, uint16_t len)
{
    uint8_t sum = 0;

    for (uint16_t i = 0; i < len; i++)
        sum += payload[i];

    return 0xFF - sum;
}

// uint8_t XBeeGetNextFrameID(void)
// {
//     static uint8_t frameId = 0; // Frame ID counter for tracking frames

//     if (++frameId == 0)
//         frameId = 1; // Avoid frame ID 0 which is reserved for no response

//     return frameId;
// }

// void XbeeInitTxTable(void)
// {
//     for (int i = 0; i < XBEE_MAX_PENDING_FRAMES; i++)
//     {
//         txTable[i].isActive = 0;
//         txTable[i].txStatus.frameId = 0;
//         txTable[i].txStatus.deliveryStatus = 0;
//     }
// }

// void XbeeResetTxTable(void)
// {
//     XbeeInitTxTable();
// }

// void XbeeAddTxFrame(uint8_t frameId)
// {
//     for (int i = 0; i < XBEE_MAX_PENDING_FRAMES; i++)
//         if (!txTable[i].isActive)
//         {
//             txTable[i].txStatus.frameId = frameId;
//             txTable[i].txStatus.deliveryStatus = 0;
//             txTable[i].isActive = 1;
//             break;
//         }
// }

// void XbeeUpdateTxStatus(XBeeAPITxStatusFrame_t *txStatus)
// {
//     for (int i = 0; i < XBEE_MAX_PENDING_FRAMES; i++)
//         if (txTable[i].isActive && txTable[i].txStatus.frameId == txStatus->frameId)
//         {
//             txTable[i].txStatus.deliveryStatus = txStatus->deliveryStatus;
//             // txTable[i].isActive = 0; // Mark as inactive after receiving status
//             break;
//         }
// }

// void XbeeGetTxStatus(uint8_t frameId, XBeeAPITxStatusFrame_t *txStatus)
// {
//     for (int i = 0; i < XBEE_MAX_PENDING_FRAMES; i++)
//         if (txTable[i].isActive && txTable[i].txStatus.frameId == frameId)
//         {
//             *txStatus = txTable[i].txStatus;
//             break;
//         }
// }

// void XbeeRemoveTxFrame(uint8_t frameId)
// {
//     for (int i = 0; i < XBEE_MAX_PENDING_FRAMES; i++)
//         if (txTable[i].isActive && txTable[i].txStatus.frameId == frameId)
//         {
//             txTable[i].isActive = 0;
//             break;
//         }
// }
