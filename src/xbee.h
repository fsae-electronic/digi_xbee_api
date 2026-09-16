/**
 * @file xbee.h
 * @brief XBee API frame builder and parser
 * @note Frames: - AT Command 0x08
 *               - Transmit Request 0x10
 *               - AT Response 0x88
 *               - TX Status 0x89
 *               - RX Packet 0x90
 */

#ifndef _XBEE_H_
#define _XBEE_H_

// Headers /////////////////////////////////////////////////////////////////////////////

#include <stddef.h>
#include <stdint.h>

// Defines /////////////////////////////////////////////////////////////////////////////

#define XBEE_API_FRAME_HEAD_SZ (3U) // StartDelimiter | Length MSB | Length LSB

#define XBEE_API_DATA_HEAD_SZ_AT_COMMAND (3U)  // FrameID | 16-bit ATCmd
#define XBEE_API_DATA_HEAD_SZ_TX_REQUEST (15U) // FrameID | 64-bit Dest | 16-bit Dest | BroadcastRadius | TxOptions
#define XBEE_API_DATA_HEAD_SZ_AT_RESPONSE (4U) // FrameID | 16-bit ATCmd | CmdStatus
#define XBEE_API_DATA_HEAD_SZ_TX_STATUS (2U)   // FrameID | DeliveryStatus
#define XBEE_API_DATA_HEAD_SZ_RX_PACKET (13U)  // 64-bit Src | 16-bit Src | RxOptions

/* ---------------------------------------------+--------------+
 * Radio                                        | Payload Size |
 * ---------------------------------------------+--------------+
 * 802.15.4 / XBee                              | 94 Bytes     |
 * XTend / XTC (Xtend Compatible) / SX products | 2048 Bytes   |
 * 900 HP                                       | 256 Bytes    |
 * ---------------------------------------------+--------------+
 */
#define XBEE_API_MAX_PAYLOAD_SZ (0xFFU) // Set max payload based on device and application requirements

#define XBEE_API_CHECKSUM_SZ (1U)

/* FrameHeader | (Max)DataHeader | Payload | Checksum */
#define XBEE_API_MAX_DATA_SZ (XBEE_API_DATA_HEAD_SZ_TX_REQUEST + XBEE_API_MAX_PAYLOAD_SZ)
#define XBEE_API_MAX_FRAME_SZ (XBEE_API_FRAME_HEAD_SZ + XBEE_API_MAX_DATA_SZ + XBEE_API_CHECKSUM_SZ)

// #define XBEE_API_MAX_PENDING_FRAMES 10

#define XBEE_API_FUNC_PROTO(func, frame) XBeeAPIStatus_t func(XBeeAPIFrame_t *frame)

// Types ///////////////////////////////////////////////////////////////////////////////

typedef enum
{
    XBEE_API_TYPE_AT_COMMAND = 0x08,
    XBEE_API_TYPE_TX_REQUEST = 0x10,
    XBEE_API_TYPE_AT_RESPONSE = 0x88,
    XBEE_API_TYPE_TX_STATUS = 0x89,
    XBEE_API_TYPE_RX_PACKET = 0x90
} XBeeAPIFrameType_t;

typedef enum
{
    // XBEE_API_RX_WAITING_START,
    // XBEE_API_RX_READING,
    XBEE_API_RX_SUCCESS,
    XBEE_API_RX_ERROR_NO_DATA,
    XBEE_API_RX_ERROR_FRAME_TOO_LARGE,
    XBEE_API_RX_ERROR_INVALID_CHECKSUM,
    XBEE_API_RX_ERROR_INVALID_FRAME,
    XBEE_API_TX_SUCCESS,
    XBEE_API_TX_ERROR_FRAME_TOO_LARGE
} XBeeAPIStatus_t;

typedef enum
{
    XBEE_API_CMD_STATUS_OK = 0x00,
    XBEE_API_CMD_STATUS_ERROR = 0x01,
    XBEE_API_CMD_STATUS_INVALID_CMD = 0x02,
    XBEE_API_CMD_STATUS_INVALID_PARAM = 0x03
} XBeeAPICommandStatus_t;

typedef enum
{
    XBEE_API_TX_STATUS_SUCCESS = 0x00,
    XBEE_API_TX_STATUS_INVALID_FRAME = 0x2C,
    XBEE_API_TX_STATUS_INTERNAL_FAILURE = 0x31,
    XBEE_API_TX_STATUS_RESOURCE_ERROR = 0x32,
    XBEE_API_TX_STATUS_PAYLOAD_TOO_LARGE = 0x74,
    XBEE_API_TX_STATUS_INVALID_HOST_ADDRESS = 0x7A,
    XBEE_API_TX_STATUS_INVALID_DATA_MODE = 0x7B,
    XBEE_API_TX_STATUS_INVALID_INTERFACE = 0x7C,
    XBEE_API_TX_STATUS_CONECTION_REFUSED = 0x80
} XBeeAPIDeliveryStatus_t;

typedef struct
{
    uint8_t frameID;
    uint8_t ATCmd[2]; // 16-bit AT command
    uint8_t *param;   // Optional parameter bytes
    uint8_t paramLen;
} XBeeAPIATCommandFrame_t;

typedef struct
{
    uint8_t frameID;
    uint64_t dest64;
    uint16_t dest16;
    uint8_t broadcastRadius;
    // struct
    // {
    //     uint8_t disableACK : 1;
    //     uint8_t disableRouteDiscovery : 1;
    //     uint8_t unicastNACK : 1;
    //     uint8_t unicastTraceRoute : 1;
    //     uint8_t reserved : 1;
    //     uint8_t deliveryMethod : 2;
    // } txOptions;
    uint8_t txOptions;
    uint8_t *payloadData;
    uint16_t payloadDataLen;
} XBeeAPITxRequestFrame_t;

typedef struct
{
    uint8_t frameID;
    uint16_t ATCmd;
    XBeeAPICommandStatus_t cmdStatus;
    uint8_t *cmdData; // Optional command bytes
    uint8_t cmdDataLen;
} XBeeAPIATResponseFrame_t;

typedef struct
{
    uint8_t frameID;
    XBeeAPIDeliveryStatus_t deliveryStatus;
} XBeeAPITxStatusFrame_t;

typedef struct
{
    uint64_t src64;
    uint16_t src16;
    // struct
    // {
    //     uint8_t packetACK : 1;
    //     uint8_t packetBroadcast : 1;
    //     uint8_t reserved : 4;
    //     uint8_t deliveryMethod : 2;
    // } rxOptions;
    uint8_t rxOptions;
    uint8_t *rxData;
    uint16_t rxDataLen;
} XBeeAPIRxPacketFrame_t;

typedef struct
{
    uint16_t length; // Frame len for Tx, data+csum len for Rx
    XBeeAPIFrameType_t type;
    union
    {
        XBeeAPIATCommandFrame_t ATCommand;
        XBeeAPITxRequestFrame_t txRequest;
        XBeeAPIATResponseFrame_t ATResponse;
        XBeeAPITxStatusFrame_t txStatus;
        XBeeAPIRxPacketFrame_t rxPacket;
    } data;
    // uint8_t checksum;

    /* This buffer is used for constructing frames before transmission and for storing
     * received frames before parsing. For Tx, store the payload data at position
     * XBEE_API_FRAME_HEAD_SZ + XBEE_API_DATA_HEAD_SZ_x and update the provided ptr. For
     * Rx, store the received bytes in this buffer and retrieve the parsed frame from
     * the appropriate structure. The ptr will point to the start of the payload.
     */
    uint8_t buffer[XBEE_API_MAX_FRAME_SZ];

    /* NOTE: XBee devices are Big-Endian */
} XBeeAPIFrame_t;

// Tx Functions ////////////////////////////////////////////////////////////////////////

/**
 * @brief Builds an XBee API frame for transmission
 * @param[in] frame Pointer to the XBeeAPIFrame_t structure to be built
 * @return XBeeAPIStatus_t The status of the operation
 */
XBEE_API_FUNC_PROTO(XBeeAPIBuildFrame, frame);

// Rx Functions ////////////////////////////////////////////////////////////////////////

/**
 * @brief Parses an incoming XBee API frame
 * @param[in] frame Pointer to the frame to be parsed
 * @return XBeeAPIStatus_t Status of the parsing operation
 */
XBEE_API_FUNC_PROTO(XBeeAPIParseFrame, frame);

////////////////////////////////////////////////////////////////////////////////////////

#endif /* _XBEE_H_ */
