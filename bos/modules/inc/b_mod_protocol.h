/**
 *!
 * \file        b_mod_protocol.h
 * \version     v0.1.0
 * \date        2020/03/15
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
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
#ifndef __B_MOD_PROTOCOL_H__
#define __B_MOD_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_PROTO_ENABLE) && (_PROTO_ENABLE == 1))

#include "b_mod_proto_type.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup PROTOCOL
 * \{
 */

/**
 * \defgroup PROTOCOL_Exported_TypesDefinitions
 * \{
 */
#if PROTO_FID_SIZE == 1
typedef uint8_t bProtoID_t;
#define INVALID_ID 0XFF
#elif PROTO_FID_SIZE == 2
typedef uint16_t bProtoID_t;
#define INVALID_ID 0XFFFF
#else
typedef uint32_t bProtoID_t;
#define INVALID_ID 0XFFFFFFFF
#endif

#if PROTO_FLEN_SIZE == 1
typedef uint8_t bProtoLen_t;
#else
typedef uint16_t bProtoLen_t;
#endif

typedef enum
{
    PROTO_CMD_TEST = 0x1,
    PROTO_CMD_UTC,
    PROTO_CMD_FW_INFO,
    PROTO_CMD_FDATA,
    PROTO_CMD_OTA_RESULT,
    PROTO_CMD_TRANS_FILE,
} bProtocolCmd_t;

#define PROTOCOL_NEED_DEFAULT_ACK(c) \
    (c == PROTO_CMD_TEST || c == PROTO_CMD_UTC || PROTO_CMD_TRANS_FILE || PROTO_CMD_FW_INFO)

/**
|      |                    |                     |       |          |       |
| :--- | ------------------ | ------------------- | ----- | -------- | ----- |
| Head | Device ID          | Len（cmd+param）    | Cmd   |  Param   | Check |
| 0xFE | sizeof(bProtoID_t) | sizeof(bProtoLen_t) | 1Byte | 0~nBytes | 1Byte |
*/
#pragma pack(1)

typedef struct
{
    uint8_t     head;
    bProtoID_t  device_id;
    bProtoLen_t len;
    uint8_t     cmd;
} bProtocolHead_t;

/// PROTO_CMD_TEST
typedef struct
{
    char str[7];
} bProtoTestParam_t;

/// PROTO_CMD_UTC
typedef struct
{
    uint32_t utc;
} bProtoUTCParam_t;

/// PROTO_CMD_FW_INFO
typedef struct
{
    uint32_t size;
    uint32_t f_crc32;
    char     filename[64];
} bProtoFWParam_t;

/// PROTO_CMD_FDATA
typedef struct
{
    uint16_t seq;
} bProtoReqFDataParam_t;

typedef struct
{
    uint16_t seq;
    uint8_t  data[512];
} bProtoFDataParam_t;

/// PROTO_CMD_OTA_RESULT
typedef struct
{
    uint8_t result;
} bProtoOTAResultParam_t;

/// PROTO_CMD_TRANS_FILE
typedef struct
{
    uint32_t size;
    uint32_t f_crc32;
    uint32_t dev_no;
    uint32_t offset;
} bProtoTransFileParam_t;

#pragma pack()

typedef int (*pdispatch)(uint8_t cmd, uint8_t *param, bProtoLen_t param_len);

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Exported_Defines
 * \{
 */

#define PROTOCOL_HEAD 0xFE

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Exported_Functions
 * \{
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

/**
 * \}
 */
#endif

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
