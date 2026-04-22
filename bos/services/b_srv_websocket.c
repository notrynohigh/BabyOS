/**
 *!
 * \file        b_srv_websocket.c
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

/*Includes ----------------------------------------------*/
#include "services/inc/b_srv_websocket.h"

#if (defined(_WEBSOCKET_SERVICE_ENABLE) && (_WEBSOCKET_SERVICE_ENABLE == 1))

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "core/inc/b_task.h"
#include "core/inc/b_timer.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
#include "modules/inc/b_mod_ssl.h"
#endif

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
 * \defgroup WEBSOCKET_Private_TypesDefinitions
 * \{
 */

typedef enum
{
    B_WS_STA_INIT,
    B_WS_STA_CONNECTING,
    B_WS_STA_HANDSHAKING,
    B_WS_STA_CONNECTED,
    B_WS_STA_DISCONNECTING,
    B_WS_STA_DISCONNECTED,
} bWebsocketState_t;

typedef struct
{
    uint8_t          fin;
    uint8_t          opcode;
    uint8_t          masked;
    uint8_t          mask_key[4];
    uint64_t         payload_len;
    uint8_t          header_len;
    uint8_t         *payload;
    uint32_t         payload_index;
} bWsFrame_t;

typedef struct
{
    bWebsocketState_t state;
    pbWebsocketCallback_t cb;
    void               *user_data;
    char               *host;
    char               *path;
    uint16_t           port;
    uint8_t            is_secure;
    int                sock_fd;
    bTaskId_t          task_id;
    bTimerId_t         timer_id;
    uint32_t           last_recv;
    bWsFrame_t         rx_frame;
    uint8_t            rx_buffer[WS_MAX_FRAME_SIZE];
    uint32_t           rx_buffer_len;
} bWebsocketInstance_t;

/**
 * \}
 */

/**
 * \defgroup WEBSOCKET_Private_Defines
 * \{
 */

#define B_WS_STA_INIT           0
#define B_WS_STA_CONNECTING     1
#define B_WS_STA_HANDSHAKING    2
#define B_WS_STA_CONNECTED       3
#define B_WS_STA_DISCONNECTING   4
#define B_WS_STA_DISCONNECTED    5

#define WS_HTTP_REQUEST_LEN_MAX 512
#define WS_KEY_LEN              24
#define WS_RESPONSE_KEY_LEN      (sizeof("Sec-WebSocket-Accept: ") + 44 + 2)  // key + \r\n

/**
 * \}
 */

/**
 * \defgroup WEBSOCKET_Private_Variables
 * \{
 */

static bWebsocketInstance_t *pbWsInstance = NULL;
static uint8_t g_ws_key_prefix[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

B_TASK_CREATE_ATTR(bWsTaskAttr);
B_TIMER_CREATE_ATTR(bWsTimerAttr);

/**
 * \}
 */

/**
 * \defgroup WEBSOCKET_Private_FunctionPrototypes
 * \{
 */

static void _bWsFree(void *addr);
static void *_bWsMalloc(uint32_t len);
static int _bWsParseUrl(const char *url, char **host, uint16_t *port, char **path, uint8_t *is_secure);
static int _bWsBase64Encode(const uint8_t *input, int len, char *output);
static int _bWsGenerateSecKey(char *key);
static int _bWsBuildHandshakeRequest(char *buf, int buf_size, const char *host, const char *path);
static int _bWsParseHandshakeResponse(const char *response, int len);
static int _bWsParseFrame(bWsFrame_t *frame, const uint8_t *data, int len);
static int _bWsSendFrame(uint8_t opcode, const uint8_t *data, uint32_t len);
static void _bWsTransCb(bTransEvent_t event, void *param, void *arg);
static void _bWsTimerCb(void *arg);

/**
 * \}
 */

/**
 * \defgroup WEBSOCKET_Private_Functions
 * \{
 */

static void _bWsFree(void *addr)
{
    if (addr)
    {
        bFree(addr);
    }
}

static void *_bWsMalloc(uint32_t len)
{
    return bMalloc(len);
}

static int _bWsBase64Encode(const uint8_t *input, int len, char *output)
{
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    uint8_t chunk[3];

    while (len--)
    {
        chunk[i++] = *(input++);
        if (i == 3)
        {
            output[j++] = base64_chars[(chunk[0] & 0xFC) >> 2];
            output[j++] = base64_chars[((chunk[0] & 0x03) << 4) | ((chunk[1] & 0xF0) >> 4)];
            output[j++] = base64_chars[((chunk[1] & 0x0F) << 2) | ((chunk[2] & 0xC0) >> 6)];
            output[j++] = base64_chars[chunk[2] & 0x3F];
            i = 0;
        }
    }

    if (i > 0)
    {
        int k = 0;
        memset(chunk + i, 0, 3 - i);
        output[j++] = base64_chars[(chunk[0] & 0xFC) >> 2];
        output[j++] = base64_chars[((chunk[0] & 0x03) << 4) | ((chunk[1] & 0xF0) >> 4)];
        output[j++] = (i == 1) ? '=' : base64_chars[((chunk[1] & 0x0F) << 2) | ((chunk[2] & 0xC0) >> 6)];
        output[j++] = '=';
    }
    output[j] = '\0';

    return j;
}

static int _bWsGenerateSecKey(char *key)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t random_bytes[16];
    int i;

    // Use simple pseudo-random based on timestamp
    uint32_t seed = bHalGetSysTick();
    for (i = 0; i < 16; i++)
    {
        seed = seed * 1103515245 + 12345;
        random_bytes[i] = (uint8_t)((seed >> 16) & 0xFF);
    }

    return _bWsBase64Encode(random_bytes, 16, key);
}

static int _bWsParseUrl(const char *url, char **host, uint16_t *port, char **path, uint8_t *is_secure)
{
    const char *ws_prefix = "ws://";
    const char *wss_prefix = "wss://";
    const char *hostname_start = NULL;
    uint8_t tmp_is_secure = 0;

    if (strncmp(url, wss_prefix, strlen(wss_prefix)) == 0)
    {
        tmp_is_secure = 1;
        hostname_start = url + strlen(wss_prefix);
    }
    else if (strncmp(url, ws_prefix, strlen(ws_prefix)) == 0)
    {
        tmp_is_secure = 0;
        hostname_start = url + strlen(ws_prefix);
    }
    else
    {
        b_log_e("ws: invalid URL scheme, must be ws:// or wss://\r\n");
        return -1;
    }

    // Find path start (first '/')
    const char *path_start = strchr(hostname_start, '/');
    uint32_t host_len = 0;
    
    // Find port start (first ':') and path
    const char *port_start = NULL;
    for (const char *p = hostname_start; *p != '\0'; p++)
    {
        if (*p == ':')
        {
            port_start = p;
        }
        else if (*p == '/')
        {
            path_start = p;
            break;
        }
    }

    if (port_start)
    {
        host_len = port_start - hostname_start;
    }
    else if (path_start)
    {
        host_len = path_start - hostname_start;
    }
    else
    {
        host_len = strlen(hostname_start);
    }

    // Extract host
    char *phost = _bWsMalloc(host_len + 1);
    if (phost == NULL)
    {
        return -1;
    }
    strncpy(phost, hostname_start, host_len);
    phost[host_len] = '\0';

    // Extract port
    uint16_t tmp_port = tmp_is_secure ? WS_SECURE_PORT : WS_DEFAULT_PORT;
    if (port_start)
    {
        int parsed_port = atoi(port_start + 1);
        if (parsed_port > 0 && parsed_port < 65536)
        {
            tmp_port = (uint16_t)parsed_port;
        }
    }

    // Extract path
    char *ppath = _bWsMalloc(64);
    if (ppath == NULL)
    {
        _bWsFree(phost);
        return -1;
    }
    if (path_start)
    {
        strncpy(ppath, path_start, 63);
        ppath[63] = '\0';
    }
    else
    {
        strcpy(ppath, "/");
    }

    *host = phost;
    *port = tmp_port;
    *path = ppath;
    *is_secure = tmp_is_secure;

    b_log("ws: parsed url: host=%s, port=%d, path=%s, secure=%d\r\n", phost, tmp_port, ppath, tmp_is_secure);
    return 0;
}

static int _bWsBuildHandshakeRequest(char *buf, int buf_size, const char *host, const char *path)
{
    char sec_key[WS_KEY_LEN + 1];
    char sec_key_encoded[64];

    _bWsGenerateSecKey(sec_key);
    _bWsBase64Encode((const uint8_t *)sec_key, WS_KEY_LEN, sec_key_encoded);

    int len = snprintf(buf, buf_size,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: BabyOS-WebSocket/1.0\r\n"
        "\r\n",
        path, host, sec_key_encoded);

    b_log("ws: handshake request (%d bytes):\r\n%s\r\n", len, buf);
    return len;
}

static int _bWsParseHandshakeResponse(const char *response, int len)
{
    // Check if response starts with "HTTP/1.1 101"
    if (strncmp(response, "HTTP/1.1 101", 12) != 0 &&
        strncmp(response, "HTTP/1.0 101", 12) != 0)
    {
        b_log_e("ws: handshake failed, not 101 Switching Protocols\r\n");
        // Print error response for debugging
        b_log_e("ws: response: %.*s\r\n", len < 200 ? len : 200, response);
        return -1;
    }

    // Check for "Sec-WebSocket-Accept" in response
    if (strstr(response, "Sec-WebSocket-Accept") == NULL)
    {
        b_log_e("ws: handshake failed, no Sec-WebSocket-Accept header\r\n");
        return -1;
    }

    b_log("ws: handshake response OK\r\n");
    return 0;
}

static int _bWsReadBytes(int sock_fd, uint8_t *buf, int len, int *read_len)
{
    int ret = bRecv(sock_fd, buf, len, (uint16_t *)read_len);
    return ret;
}

static int _bWsWriteBytes(int sock_fd, const uint8_t *buf, int len)
{
    int written = 0;
    int ret = bSend(sock_fd, buf, len, (uint16_t *)&written);
    if (ret < 0)
    {
        return -1;
    }
    return written;
}

static int _bWsParseFrame(bWsFrame_t *frame, const uint8_t *data, int len)
{
    if (len < 2)
    {
        return -1;
    }

    memset(frame, 0, sizeof(bWsFrame_t));

    // Byte 0: FIN + opcode
    frame->fin = (data[0] & 0x80) != 0;
    frame->opcode = data[0] & 0x0F;

    // Byte 1: MASK + payload length
    frame->masked = (data[1] & 0x80) != 0;
    uint8_t payload_len_byte = data[1] & 0x7F;

    frame->header_len = 2;

    // Extended payload length (7, 16, or 64 bits)
    if (payload_len_byte < 126)
    {
        frame->payload_len = payload_len_byte;
    }
    else if (payload_len_byte == 126)
    {
        if (len < 4)
        {
            return -1;
        }
        frame->payload_len = ((uint16_t)data[2] << 8) | data[3];
        frame->header_len = 4;
    }
    else if (payload_len_byte == 127)
    {
        if (len < 10)
        {
            return -1;
        }
        // Read 8-byte extended length (we only support up to 4GB for now)
        frame->payload_len = 0;
        for (int i = 0; i < 8; i++)
        {
            frame->payload_len = (frame->payload_len << 8) | data[2 + i];
        }
        frame->header_len = 10;
    }

    // Masking key (if masked)
    if (frame->masked)
    {
        if ((int)frame->header_len + 4 > len)
        {
            return -1;
        }
        memcpy(frame->mask_key, data + frame->header_len, 4);
        frame->header_len += 4;
    }

    // Check if we have the complete frame
    if ((int)frame->header_len + (int)frame->payload_len > len)
    {
        return -1;  // Not enough data yet
    }

    // Get pointer to payload data
    frame->payload = (uint8_t *)(data + frame->header_len);

    // Decode mask if needed
    if (frame->masked)
    {
        // We need to decode in-place - but we don't have the full original buffer
        // For now, just point to the masked data (caller should handle unmasking)
    }

    b_log("ws: frame parsed: fin=%d, opcode=%d, masked=%d, payload_len=%d, header_len=%d\r\n",
          frame->fin, frame->opcode, frame->masked, (int)frame->payload_len, frame->header_len);

    return 0;
}

static uint8_t _bWsBuildFrameHeader(uint8_t *buf, uint8_t opcode, uint64_t payload_len, uint8_t masked)
{
    buf[0] = (0x80 | opcode);  // FIN + opcode

    if (payload_len < 126)
    {
        buf[1] = (masked ? 0x80 : 0x00) | (uint8_t)payload_len;
        return 2;
    }
    else if (payload_len < 65536)
    {
        buf[1] = (masked ? 0x80 : 0x00) | 126;
        buf[2] = (uint8_t)(payload_len >> 8);
        buf[3] = (uint8_t)(payload_len & 0xFF);
        return 4;
    }
    else
    {
        buf[1] = (masked ? 0x80 : 0x00) | 127;
        // 8-byte length
        for (int i = 7; i >= 0; i--)
        {
            buf[2 + i] = (uint8_t)(payload_len & 0xFF);
            payload_len >>= 8;
        }
        return 10;
    }
}

static int _bWsSendFrame(uint8_t opcode, const uint8_t *data, uint32_t len)
{
    if (pbWsInstance == NULL || pbWsInstance->sock_fd < 0)
    {
        return -1;
    }

    uint8_t header[14];
    uint8_t mask_key[4] = {0};  // No masking for outbound frames from client to server
    uint8_t header_len = _bWsBuildFrameHeader(header, opcode, len, 0);

    // For sending, we use non-masked frames (server-side doesn't mask)
    // Build complete frame in a temp buffer
    uint8_t *frame_buf = _bWsMalloc(header_len + len);
    if (frame_buf == NULL)
    {
        return -1;
    }

    memcpy(frame_buf, header, header_len);
    if (data && len > 0)
    {
        memcpy(frame_buf + header_len, data, len);
    }

    int ret = _bWsWriteBytes(pbWsInstance->sock_fd, frame_buf, header_len + len);
    _bWsFree(frame_buf);

    if (ret < 0)
    {
        return -1;
    }

    b_log("ws: sent frame: opcode=%d, len=%d\r\n", opcode, len);
    return 0;
}

static void _bWsTransCb(bTransEvent_t event, void *param, void *arg)
{
    bWebsocketInstance_t *pinstance = (bWebsocketInstance_t *)arg;
    if (event == B_TRANS_DISCONNECT)
    {
        b_log("ws: transport disconnect event\r\n");
        if (pinstance && pinstance->state != B_WS_STA_INIT)
        {
            pinstance->state = B_WS_STA_DISCONNECTED;
        }
    }
}

static void _bWsTimerCb(void *arg)
{
    bWebsocketInstance_t *pinstance = (bWebsocketInstance_t *)arg;
    if (pinstance == NULL)
    {
        return;
    }

    // Check for stale connection (no data received for long time)
    if (pinstance->state == B_WS_STA_CONNECTED)
    {
        if ((TICK_DIFF_BIT32(pinstance->last_recv, bHalGetSysTick())) > MS2TICKS(60000))
        {
            b_log_w("ws: connection timeout, no data received\r\n");
            pinstance->state = B_WS_STA_DISCONNECTED;
        }
    }
}

static int _bWsHandleReceivedData(uint8_t *data, int len)
{
    bWsFrame_t frame;
    int offset = 0;

    while (offset < len)
    {
        int ret = _bWsParseFrame(&frame, data + offset, len - offset);
        if (ret < 0)
        {
            // Not enough data for complete frame
            break;
        }

        bWebsocketEvent_t evt = B_WEBSOCKET_EVT_INVALID;
        bWebsocketEvtParam_t param = {0};

        switch (frame.opcode)
        {
            case WS_OPCODE_TEXT:
            case WS_OPCODE_BINARY:
            {
                // Unmask payload data
                uint8_t *unmasked_data = _bWsMalloc((uint32_t)frame.payload_len + 1);
                if (unmasked_data)
                {
                    if (frame.masked && frame.mask_key[0] != 0)
                    {
                        for (uint32_t i = 0; i < frame.payload_len; i++)
                        {
                            unmasked_data[i] = frame.payload[i] ^ frame.mask_key[i % 4];
                        }
                    }
                    else
                    {
                        memcpy(unmasked_data, frame.payload, (uint32_t)frame.payload_len);
                    }
                    unmasked_data[frame.payload_len] = '\0';

                    param.msg.data = unmasked_data;
                    param.msg.len = (uint32_t)frame.payload_len;
                    evt = (frame.opcode == WS_OPCODE_TEXT) ? B_WEBSOCKET_EVT_TEXT : B_WEBSOCKET_EVT_BINARY;
                }
                break;
            }

            case WS_OPCODE_CLOSE:
            {
                b_log("ws: received close frame\r\n");
                evt = B_WEBSOCKET_EVT_DISCONN;
                break;
            }

            case WS_OPCODE_PING:
            {
                b_log("ws: received ping, sending pong\r\n");
                // Send pong response with same payload
                if (frame.payload_len > 0)
                {
                    _bWsSendFrame(WS_OPCODE_PONG, frame.payload, (uint32_t)frame.payload_len);
                }
                else
                {
                    _bWsSendFrame(WS_OPCODE_PONG, NULL, 0);
                }
                evt = B_WEBSOCKET_EVT_PING;
                break;
            }

            case WS_OPCODE_PONG:
            {
                b_log("ws: received pong\r\n");
                evt = B_WEBSOCKET_EVT_PONG;
                if (frame.payload_len > 0)
                {
                    param.msg.data = frame.payload;
                    param.msg.len = (uint32_t)frame.payload_len;
                }
                break;
            }

            case WS_OPCODE_CONTINUE:
            {
                b_log_w("ws: continue frame not supported yet\r\n");
                break;
            }

            default:
            {
                b_log_w("ws: unknown opcode: %d\r\n", frame.opcode);
                break;
            }
        }

        if (evt != B_WEBSOCKET_EVT_INVALID && pbWsInstance->cb)
        {
            pbWsInstance->cb(evt, &param, pbWsInstance->user_data);
        }

        if (param.msg.data)
        {
            _bWsFree(param.msg.data);
        }

        offset += frame.header_len + (int)frame.payload_len;
    }

    return offset;
}

PT_THREAD(_bWsTaskFunc)(struct pt *pt, void *arg)
{
    static int sock_fd = -1;
    static uint8_t handshake_buf[512];
    static int handshake_received = 0;
    
    bWebsocketInstance_t *pinstance = (bWebsocketInstance_t *)arg;
    bWebsocketEvent_t evt = B_WEBSOCKET_EVT_INVALID;
    bWebsocketEvtParam_t param = {0};

    B_TASK_INIT_BEGIN();
    // ...
    B_TASK_INIT_END();

    PT_BEGIN(pt);

    while (1)
    {
        if (pinstance == NULL)
        {
            bTaskRestart(pt);
        }

        switch (pinstance->state)
        {
            case B_WS_STA_INIT:
            {
                b_log("ws: connecting to %s:%d...\r\n", pinstance->host, pinstance->port);
                
                // Create socket
                sock_fd = bSocket(B_TRANS_CONN_TCP, _bWsTransCb, pinstance);
                if (SOCKFD_IS_INVALID(sock_fd))
                {
                    b_log_e("ws: socket create failed\r\n");
                    pinstance->state = B_WS_STA_DISCONNECTED;
                    bTaskRestart(pt);
                }

                // Connect
                if (bConnect(sock_fd, pinstance->host, pinstance->port) < 0)
                {
                    b_log_e("ws: connect failed\r\n");
                    SOCKET_SHUTDOWN(pt, sock_fd);
                    pinstance->state = B_WS_STA_DISCONNECTED;
                    bTaskRestart(pt);
                }

                // Wait for connection
                PT_WAIT_UNTIL(pt, bSocketIsConnected(sock_fd) == 1, MS2TICKS(5000));
                if (PT_WAIT_IS_TIMEOUT(pt))
                {
                    b_log_e("ws: connection timeout\r\n");
                    SOCKET_SHUTDOWN(pt, sock_fd);
                    pinstance->state = B_WS_STA_DISCONNECTED;
                    bTaskRestart(pt);
                }

                pinstance->sock_fd = sock_fd;
                pinstance->state = B_WS_STA_HANDSHAKING;
                handshake_received = 0;
                
                // Send handshake request
                memset(handshake_buf, 0, sizeof(handshake_buf));
                int req_len = _bWsBuildHandshakeRequest((char *)handshake_buf, 
                                                       sizeof(handshake_buf) - 1,
                                                       pinstance->host, 
                                                       pinstance->path);
                if (_bWsWriteBytes(sock_fd, handshake_buf, req_len) < 0)
                {
                    b_log_e("ws: failed to send handshake\r\n");
                    SOCKET_SHUTDOWN(pt, sock_fd);
                    pinstance->state = B_WS_STA_DISCONNECTED;
                    bTaskRestart(pt);
                }
                break;
            }

            case B_WS_STA_HANDSHAKING:
            {
                // Wait for handshake response
                int read_len = 0;
                uint8_t ch;
                
                PT_WAIT_UNTIL(pt, bSockIsReadable(sock_fd), 5000);
                if (PT_WAIT_IS_TIMEOUT(pt))
                {
                    b_log_e("ws: handshake timeout\r\n");
                    SOCKET_SHUTDOWN(pt, sock_fd);
                    pinstance->state = B_WS_STA_DISCONNECTED;
                    bTaskRestart(pt);
                }

                // Read byte by byte until we have the full HTTP response
                while (handshake_received < (int)sizeof(handshake_buf) - 1)
                {
                    int ret = _bWsReadBytes(sock_fd, &ch, 1, &read_len);
                    if (ret < 0 || read_len == 0)
                    {
                        break;
                    }
                    handshake_buf[handshake_received++] = ch;
                    
                    // Check for end of HTTP headers (\r\n\r\n)
                    if (handshake_received >= 4 &&
                        handshake_buf[handshake_received - 4] == '\r' &&
                        handshake_buf[handshake_received - 3] == '\n' &&
                        handshake_buf[handshake_received - 2] == '\r' &&
                        handshake_buf[handshake_received - 1] == '\n')
                    {
                        break;
                    }
                }

                handshake_buf[handshake_received] = '\0';
                
                if (_bWsParseHandshakeResponse((char *)handshake_buf, handshake_received) < 0)
                {
                    SOCKET_SHUTDOWN(pt, sock_fd);
                    pinstance->state = B_WS_STA_DISCONNECTED;
                    bTaskRestart(pt);
                }

                b_log("ws: handshake success, connected!\r\n");
                pinstance->state = B_WS_STA_CONNECTED;
                pinstance->last_recv = bHalGetSysTick();
                bTimerStart(pinstance->timer_id, 30000);

                evt = B_WEBSOCKET_EVT_CONN;
                if (pinstance->cb)
                {
                    pinstance->cb(evt, &param, pinstance->user_data);
                }
                break;
            }

            case B_WS_STA_CONNECTED:
            {
                // Wait for data or disconnect
                PT_WAIT_UNTIL(pt, 
                              bSockIsReadable(sock_fd) || 
                              pinstance->state != B_WS_STA_CONNECTED, 
                              1000);

                if (pinstance->state != B_WS_STA_CONNECTED)
                {
                    b_log("ws: state changed, exiting connected state\r\n");
                    break;
                }

                if (bSockIsReadable(sock_fd))
                {
                    uint8_t recv_buf[512];
                    int read_len = 0;

                    int ret = _bWsReadBytes(sock_fd, recv_buf, sizeof(recv_buf), &read_len);
                    if (ret < 0 || read_len == 0)
                    {
                        b_log_w("ws: connection lost\r\n");
                        pinstance->state = B_WS_STA_DISCONNECTED;
                        break;
                    }

                    pinstance->last_recv = bHalGetSysTick();

                    // Handle received WebSocket frames
                    int consumed = _bWsHandleReceivedData(recv_buf, read_len);
                    if (consumed < read_len)
                    {
                        b_log_w("ws: %d bytes not fully consumed\r\n", read_len - consumed);
                    }
                }
                break;
            }

            case B_WS_STA_DISCONNECTING:
            {
                // Send close frame
                _bWsSendFrame(WS_OPCODE_CLOSE, NULL, 0);
                SOCKET_SHUTDOWN(pt, sock_fd);
                pinstance->state = B_WS_STA_DISCONNECTED;
                sock_fd = -1;
                evt = B_WEBSOCKET_EVT_DISCONN;
                if (pinstance->cb)
                {
                    pinstance->cb(evt, &param, pinstance->user_data);
                }
                bTaskRestart(pt);
                break;
            }

            case B_WS_STA_DISCONNECTED:
            {
                // Wait a bit before reconnecting
                PT_WAIT_UNTIL(pt, 0, 1000);
                bTaskRestart(pt);
                break;
            }
        }
    }

    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup WEBSOCKET_Exported_Functions
 * \{
 */

int bWebsocketSrvStart(pbWebsocketCallback_t cb, void *arg, const char *url)
{
    bWebsocketInstance_t *pinstance = NULL;
    char *host = NULL;
    char *path = NULL;
    uint16_t port = 0;
    uint8_t is_secure = 0;

    if (pbWsInstance != NULL)
    {
        b_log_e("ws: service already started\r\n");
        return -1;
    }

    if (cb == NULL || url == NULL)
    {
        b_log_e("ws: invalid parameters\r\n");
        return -1;
    }

    // Parse URL
    if (_bWsParseUrl(url, &host, &port, &path, &is_secure) < 0)
    {
        b_log_e("ws: URL parse failed\r\n");
        return -1;
    }

    // Allocate instance
    pinstance = bCalloc(1, sizeof(bWebsocketInstance_t));
    if (pinstance == NULL)
    {
        _bWsFree(host);
        _bWsFree(path);
        return -1;
    }

    pinstance->cb = cb;
    pinstance->user_data = arg;
    pinstance->host = host;
    pinstance->path = path;
    pinstance->port = port;
    pinstance->is_secure = is_secure;
    pinstance->sock_fd = -1;
    pinstance->state = B_WS_STA_INIT;
    pinstance->last_recv = bHalGetSysTick();

    // Create task
    pinstance->task_id = bTaskCreate("websocket", _bWsTaskFunc, pinstance, &bWsTaskAttr);
    if (pinstance->task_id <= 0)
    {
        _bWsFree(host);
        _bWsFree(path);
        bFree(pinstance);
        return -1;
    }

    // Create timer
    pinstance->timer_id = bTimerCreate(_bWsTimerCb, B_TIMER_PERIODIC, pinstance, &bWsTimerAttr);

    pbWsInstance = pinstance;
    b_log("ws: service started with URL: %s\r\n", url);

    return 0;
}

int bWebsocketSrvSendText(const char *data, uint32_t len)
{
    if (pbWsInstance == NULL || pbWsInstance->state != B_WS_STA_CONNECTED)
    {
        b_log_e("ws: not connected\r\n");
        return -1;
    }
    return _bWsSendFrame(WS_OPCODE_TEXT, (const uint8_t *)data, len);
}

int bWebsocketSrvSendBinary(const uint8_t *data, uint32_t len)
{
    if (pbWsInstance == NULL || pbWsInstance->state != B_WS_STA_CONNECTED)
    {
        b_log_e("ws: not connected\r\n");
        return -1;
    }
    return _bWsSendFrame(WS_OPCODE_BINARY, data, len);
}

int bWebsocketSrvSendPing(const uint8_t *data, uint32_t len)
{
    if (pbWsInstance == NULL || pbWsInstance->state != B_WS_STA_CONNECTED)
    {
        b_log_e("ws: not connected\r\n");
        return -1;
    }
    return _bWsSendFrame(WS_OPCODE_PING, data, len);
}

int bWebsocketSrvSendPong(const uint8_t *data, uint32_t len)
{
    if (pbWsInstance == NULL || pbWsInstance->state != B_WS_STA_CONNECTED)
    {
        b_log_e("ws: not connected\r\n");
        return -1;
    }
    return _bWsSendFrame(WS_OPCODE_PONG, data, len);
}

int bWebsocketSrvClose(void)
{
    if (pbWsInstance == NULL)
    {
        return -1;
    }
    if (pbWsInstance->state == B_WS_STA_CONNECTED)
    {
        pbWsInstance->state = B_WS_STA_DISCONNECTING;
        return 0;
    }
    return -1;
}

int bWebsocketSrvGetStatus(void)
{
    if (pbWsInstance == NULL)
    {
        return -1;
    }
    return (pbWsInstance->state == B_WS_STA_CONNECTED) ? 1 : 0;
}

void bWebsocketSrvDestroy(void)
{
    if (pbWsInstance == NULL)
    {
        return;
    }

    bWebsocketInstance_t *pinstance = pbWsInstance;
    pbWsInstance = NULL;

    if (pinstance->task_id > 0)
    {
        bTaskRemove(pinstance->task_id);
    }
    if (pinstance->timer_id > 0)
    {
        bTimerDelete(pinstance->timer_id);
    }
    if (pinstance->host)
    {
        _bWsFree(pinstance->host);
    }
    if (pinstance->path)
    {
        _bWsFree(pinstance->path);
    }

    bFree(pinstance);
    b_log("ws: service destroyed\r\n");
}

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

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/