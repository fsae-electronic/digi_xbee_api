/**
 * @file xbee.h
 * @brief XBee API frame builder and parser
 * @note Frames: - AT Command 0x08
 *               - Transmit Request 0x10
 *               - AT Response 0x88
 *               - TX Status 0x8B
 *               - RX Packet 0x90
 */

#ifndef _XBEE_H_
#define _XBEE_H_

// Headers /////////////////////////////////////////////////////////////////////////////

#include <stddef.h>
#include <stdint.h>

// Defines /////////////////////////////////////////////////////////////////////////////

#define MAX_PAYLOAD_SIZE 256

// Types ///////////////////////////////////////////////////////////////////////////////

typedef enum
{
    XBEE_API_TYPE_AT_COMMAND = 0x08,
    XBEE_API_TYPE_TX_REQUEST = 0x10,
    XBEE_API_TYPE_AT_RESPONSE = 0x88,
    XBEE_API_TYPE_TX_STATUS = 0x8B,
    XBEE_API_TYPE_RX_PACKET = 0x90,
} XBeeAPIFrameType_t;

typedef enum
{
    // XBEE_API_RX_WAITING_START,
    // XBEE_API_RX_READING,
    XBEE_API_RX_SUCCESS,
    // XBEE_API_RX_ERROR_FRAME_TOO_LARGE,
    XBEE_API_RX_ERROR_INVALID_CHECKSUM,
    XBEE_API_RX_ERROR_INVALID_FRAME,
    XBEE_API_TX_SUCCESS,
    XBEE_API_TX_ERROR_FRAME_TOO_LARGE,
} XBeeAPIStatus_t;

typedef struct
{
    uint8_t frameId;
    uint8_t atCmd[2];
    uint8_t *param;
    uint8_t paramLen;
} XBeeAPIATResponseFrame_t;

typedef struct
{
    uint8_t frameId;
    uint8_t deliveryStatus;
} XBeeAPITxStatusFrame_t;

typedef struct
{
    uint8_t rssi;
    uint8_t *rfData;
    size_t rfDataLen;
} XBeeAPIRxPacketFrame_t;

// typedef struct
// {
//     // uint16_t length; // MSB first
//     XBeeAPIFrameType_t type;
//     void *payload;
//     // uint8_t checksum;
// } XBeeAPIFrame_t;

typedef struct
{
    XBeeAPIFrameType_t type;
    union
    {
        XBeeAPIATResponseFrame_t atResponse;
        XBeeAPITxStatusFrame_t txStatus;
        XBeeAPIRxPacketFrame_t rxPacket;
    };
} XBeeAPIParsedFrame_t;

// Tx Functions ////////////////////////////////////////////////////////////////////////

/**
 * @brief Builds an AT Command frame
 * @param[in] atCmd The AT command string (2 characters)
 * @param[in] param Pointer to the parameter bytes
 * @param[in] paramLen Length of the parameter bytes
 * @param[out] frame Pointer to the buffer where the constructed frame will be stored
 * @param[out] frameID Pointer to a variable where the assigned FrameID will be stored
 * @return XBeeAPIStatus_t The status of the operation
 */
XBeeAPIStatus_t XBeeBuildAT(const char *atCmd, const uint8_t *param, uint8_t paramLen,
                            uint8_t **frame, uint8_t *frameID);

/**
 * @brief Builds a Transmit Request frame
 * @param[in] dest64 64-bit destination address
 * @param[in] dest16 16-bit destination address
 * @param[in] rfData Pointer to the RF data bytes
 * @param[in] rfLen Length of the RF data bytes
 * @param[out] frame Pointer to the buffer where the constructed frame will be stored
 * @param[out] frameID Pointer to a variable where the assigned FrameID will be stored
 * @return XBeeAPIStatus_t The status of the operation
 */
XBeeAPIStatus_t XBeeBuildTxRequest(const uint8_t *dest64, const uint8_t *dest16, const uint8_t *rfData, size_t rfLen,
                                   uint8_t **frame, uint8_t *frameID);

// Rx Functions ////////////////////////////////////////////////////////////////////////

/**
 * @brief Gets the length of the incoming XBee API frame
 * @param[in] c The incoming character
 * @return uint16_t The length of the frame
 */
uint16_t XBeeRxLen(uint8_t c);

/**
 * @brief Parses an incoming XBee API frame
 * @param[in] payload Pointer to the API frame payload
 * @param[in] len Length of the payload (includes checksum)
 * @param[out] parsedFrame Pointer to the structure where the data will be stored
 * @return XBeeAPIRxStatus_t Status of the parsing operation
 */
XBeeAPIStatus_t XBeeParseFrame(const uint8_t *payload, size_t len,
                               XBeeAPIParsedFrame_t *parsedFrame);
// void XBeeRx(uint8_t c);

////////////////////////////////////////////////////////////////////////////////////////

#endif /* _XBEE_H_ */
