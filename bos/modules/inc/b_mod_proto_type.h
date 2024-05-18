/**
 *!
 * \file        b_mod_proto_type.h
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
#ifndef __B_MOD_PROTO_TYPE_H__
#define __B_MOD_PROTO_TYPE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_section.h"
#include "utils/inc/b_util_list.h"

#pragma pack(1)

typedef struct
{
    uint8_t  seq;
    uint8_t *dat;
    uint16_t dat_len;
} bXYModemCbParam_t;

typedef struct
{
    uint8_t  slave_addr;
    uint8_t  reserved1;
    uint16_t base_reg;      // Little endian
    uint16_t reg_value[1];  // Little endian
} bModbusMasterWriteReg_t;

typedef struct
{
    uint8_t  slave_addr;
    uint8_t  reserved1;
    uint16_t base_reg;  // Little endian
    uint16_t reg_num;   // Little endian
    uint8_t  reserved2;
    uint16_t reg_value[1];  // Little endian
} bModbusMasterWriteRegs_t;

typedef struct
{
    uint8_t  slave_addr;
    uint8_t  reserved;
    uint16_t base_reg;  // Little endian
    uint16_t reg_num;   // Little endian
} bModbusMasterRead_t;

typedef struct
{
    uint8_t  slave_addr;
    uint8_t  reserved;
    uint16_t base_reg;   // Little endian
    uint16_t reg_value;  // Little endian
    uint16_t crc;
} bModbusSlaveWriteReg_t;

typedef struct
{
    uint8_t  slave_addr;
    uint8_t  reserved;
    uint16_t base_reg;  // Little endian
    uint16_t reg_num;   // Little endian
    uint16_t crc;
} bModbusSlaveWriteRegs_t;

typedef struct
{
    uint8_t  slave_addr;
    uint8_t  reserved;
    uint8_t  len;
    uint16_t reg_value[1];  // Little endian
} bModbusSlaveRead_t;

typedef struct
{
    uint8_t   slave_id;
    uint8_t   func_code;
    uint16_t  base_reg;   // Little endian
    uint16_t  reg_num;    // Little endian
    uint16_t *reg_value;  // Little endian
} bModbusCbParm_t;

typedef struct
{
    uint8_t  name[64];
    uint32_t size;
    uint32_t fcrc32;
} bProtoFileInfo_t;

typedef struct
{
    uint32_t dev;
    uint32_t offset;
} bProtoFileLocation_t;

typedef struct
{
    uint32_t offset;
    uint32_t size;
} bProtoReqFileData_t;

typedef struct
{
    uint32_t offset;
    uint32_t size;
    uint8_t *dat;
} bProtoFileData_t;

typedef struct
{
    uint32_t utc;
    float    timezone;
} bProtoUtc_t;

#define B_PROTO_TRANS_RESULT_OK 0
#define B_PROTO_TRANS_RESULT_FAIL 1

#pragma pack()

typedef enum
{
    B_XYMODEM_CMD_START,        // package [null],
    B_XYMODEM_CMD_STOP,         // package [null]
    B_XYMODEM_DATA,             // callback [bXYModemCbParam_t]
    B_MODBUS_CMD_READ_REG,      // pakage [bModbusMasterRead_t], callback [bModbusCbParm_t]
    B_MODBUS_CMD_WRITE_REG,     // pakage [bModbusMasterWriteRegs_t], callback [bModbusCbParm_t]
    B_MODBUS_CMD_WRITE_REGS,    // pakage [bModbusMasterWriteRegs_t], callback [bModbusCbParm_t]
    B_PROTO_TRANS_FILE_INFO,    // callback [bProtoFileInfo_t]
    B_PROTO_OTA_FILE_INFO,      // callback [bProtoFileInfo_t]
    B_PROTO_SET_FILE_LOCATION,  // callback [bProtoFileLocation_t]
    B_PROTO_REQ_FILE_DATA,      // package [bProtoReqFileData_t]
    B_PROTO_FILE_DATA,          // callback [bProtoFileData_t]
    B_PROTO_TRANS_FILE_RESULT,  // package [uint8_t]
    B_PROTO_UTC,                // callback [bProtoUtc_t]
    B_PROTO_CMD_NUMBER,
} bProtoCmd_t;

typedef enum
{
    B_PROTO_INFO_DEVICE_ID,              // 获取设备id
    B_PROTO_INFO_MODBUS_REG_PERMISSION,  // 获取modbus寄存器的读写权限
    B_PROTO_INFO_NUMBER,
} bProtoInfoType_t;

typedef int (*bProtoUserCallback_t)(bProtoCmd_t cmd, void *param);
typedef int (*bProtoCallback_t)(bProtoCmd_t cmd, void *param, void *arg);
typedef int (*bProtoParse_t)(void *attr, uint8_t *in, uint16_t i_len, uint8_t *out, uint16_t o_len);
typedef int (*bProtoPackage_t)(void *attr, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len);
typedef int (*bProtoGetInfo_t)(bProtoInfoType_t type, uint8_t *buf, uint16_t buf_len);

typedef struct
{
    uint32_t             reserved;
    bProtoParse_t        parse;
    bProtoPackage_t      package;
    bProtoGetInfo_t      get_info;
    bProtoUserCallback_t u_callback;
    bProtoCallback_t     callback;
    void                *arg;
    struct list_head     list;
} bProtocolAttr_t;

typedef struct
{
    const char     *name;
    bProtoParse_t   parse;
    bProtoPackage_t package;
} bProtocolInstance_t;

#define bPROTOCOL_REG_INSTANCE(proto_name, _parse, _package)                                     \
    bSECTION_ITEM_REGISTER_FLASH(b_srv_protocol, bProtocolInstance_t, CONCAT_2(do_, _parse)) = { \
        .name = proto_name, .parse = _parse, .package = _package};

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
