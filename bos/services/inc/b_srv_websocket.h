/**
 *!
 * \file        b_srv_websocket.h
 * \version     v0.0.1
 * \date        2026/04/22
 * \author      aiclaw
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 aiclaw
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_SRV_WEBSOCKET_H__
#define __B_SRV_WEBSOCKET_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_WEBSOCKET_SERVICE_ENABLE) && (_WEBSOCKET_SERVICE_ENABLE == 1))

#include "modules/inc/b_mod_tcpip.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup WEBSOCKET
 * \{
 */

/**
 * \defgroup WEBSOCKET_Exported_TypesDefinitions
 * \{
 */

typedef enum
{
    B_WEBSOCKET_EVT_CONN,      // Connection established
    B_WEBSOCKET_EVT_DISCONN,    // Disconnected
    B_WEBSOCKET_EVT_TEXT,       // Text message received
    B_WEBSOCKET_EVT_BINARY,     // Binary message received
    B_WEBSOCKET_EVT_PING,       // Ping received (should respond with pong)
    B_WEBSOCKET_EVT_PONG,       // Pong received
    B_WEBSOCKET_EVT_ERROR,      // Error occurred
    B_WEBSOCKET_EVT_INVALID,
} bWebsocketEvent_t;

typedef struct
{
    uint8_t *data;
    uint32_t len;
} bWebsocketData_t;

typedef union
{
    bWebsocketData_t msg;
} bWebsocketEvtParam_t;

typedef void (*pbWebsocketCallback_t)(bWebsocketEvent_t evt, bWebsocketEvtParam_t *param, void *user_data);

// WebSocket opcode
#define WS_OPCODE_CONTINUE   0x0
#define WS_OPCODE_TEXT       0x1
#define WS_OPCODE_BINARY     0x2
#define WS_OPCODE_CLOSE      0x8
#define WS_OPCODE_PING       0x9
#define WS_OPCODE_PONG       0xA

/**
 * \}
 */

/**
 * \defgroup WEBSOCKET_Exported_Defines
 * \{
 */

#define WS_MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define WS_DEFAULT_PORT      80
#define WS_SECURE_PORT       443
#define WS_MAX_FRAME_SIZE    4096
#define WS_MAX_HEADER_SIZE   14  // 2 + 8 + 4 (max extended payload)

/**
 * \}
 */

/**
 * \defgroup WEBSOCKET_Exported_Functions
 * \{
 */

// Start WebSocket client with URL
int bWebsocketSrvStart(pbWebsocketCallback_t cb, void *arg, const char *url);

// Send text message
int bWebsocketSrvSendText(const char *data, uint32_t len);

// Send binary message
int bWebsocketSrvSendBinary(const uint8_t *data, uint32_t len);

// Send ping (active ping)
int bWebsocketSrvSendPing(const uint8_t *data, uint32_t len);

// Send pong (response to peer's ping)
int bWebsocketSrvSendPong(const uint8_t *data, uint32_t len);

// Close WebSocket connection gracefully
int bWebsocketSrvClose(void);

// Get connection status (1=connected, 0=disconnected, -1=not initialized)
int bWebsocketSrvGetStatus(void);

// Destroy WebSocket service
void bWebsocketSrvDestroy(void);

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#endif

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/