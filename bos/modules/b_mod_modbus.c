/**
 *!
 * \file        b_mod_modbus.c
 * \version     v0.0.2
 * \date        2020/05/13
 * \author      Bean(notrynohigh@outlook.com)
 * \note        This is not a complete Modbus-RTU stack. It only supports
 *              function code: 03(read holding registers), 16(preset multiple registers).
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

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_modbus.h"
#if (defined(_MODBUS_ENABLE) && (_MODBUS_ENABLE == 1))
#include <string.h>

#include "algorithm/inc/algo_crc.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup MODBUS
 * \{
 */

/**
 * \defgroup MODBUS_Private_TypesDefinitions
 * \{
 */
#pragma pack(1)

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint16_t crc;  // Little endian
} bModbusReadRegs_t;

typedef struct
{
    uint8_t addr;
    uint8_t func;
    uint8_t len;
    uint8_t buf[1];
} bModbusReadRegsAck_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint8_t  len;
    uint16_t param[1];
} bModbusWriteRegs_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t value;
    uint16_t crc;  // Little endian
} bModbusWriteReg_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint16_t crc;  // Little endian
} bModbusWriteRegsAck_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t value;
    uint16_t crc;  // Little endian
} bModbusWriteRegAck_t;

#pragma pack()
/**
 * \}
 */

/**
 * \defgroup MODBUS_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MODBUS_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MODBUS_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MODBUS_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MODBUS_Private_Functions
 * \{
 */

static uint16_t _bMBCRC16(uint8_t *pucFrame, uint16_t usLen)
{
    uint32_t crc_ret = crc_calculate(ALGO_CRC16_MODBUS, pucFrame, usLen);
    return ((uint16_t)(crc_ret & 0xffff));
}

static int _bModbusRTUParse(void *attr, uint8_t *in, uint16_t i_len, uint8_t *out, uint16_t o_len)
{
    int              len   = 0;
    int              i     = 0;
    uint8_t          tmp   = 0;
    bProtocolAttr_t *pattr = (bProtocolAttr_t *)attr;
    bModbusCbParm_t  param;

    if (i_len < 2)
    {
        return -1;
    }

    if (in[1] == MODBUS_RTU_READ_REGS)
    {
        bModbusReadRegsAck_t *r_ack = (bModbusReadRegsAck_t *)in;
        len                         = sizeof(bModbusReadRegsAck_t) - 1 + r_ack->len + 2;
        if (i_len < len)
        {
            return -1;
        }
        param.slave_id  = r_ack->addr;
        param.func_code = r_ack->func;
        param.base_reg  = 0;
        param.reg_num   = r_ack->len / 2;
        for (i = 0; i < param.reg_num; i++)
        {
            tmp                   = r_ack->buf[i * 2];
            r_ack->buf[i * 2]     = r_ack->buf[i * 2 + 1];
            r_ack->buf[i * 2 + 1] = tmp;
        }
        param.reg_value = (uint16_t *)r_ack->buf;
        B_SAFE_INVOKE(pattr->callback, B_MODBUS_CMD_READ_REG, &param);
    }
    else if (in[1] == MODBUS_RTU_WRITE_REG)
    {
        bModbusWriteRegAck_t *w_1_ack = (bModbusWriteRegAck_t *)in;
        len                           = sizeof(bModbusWriteRegAck_t);
        if (i_len < len)
        {
            return -1;
        }
        param.slave_id  = w_1_ack->addr;
        param.func_code = w_1_ack->func;
        param.base_reg  = L2B_B2L_16b(w_1_ack->reg);
        param.reg_num   = 1;
        w_1_ack->value  = L2B_B2L_16b(w_1_ack->value);
        param.reg_value = (uint16_t *)&w_1_ack->value;
        B_SAFE_INVOKE(pattr->callback, B_MODBUS_CMD_WRITE_REG, &param);
    }
    else if (in[1] == MODBUS_RTU_WRITE_REGS)
    {
        bModbusWriteRegsAck_t *w_ack = (bModbusWriteRegsAck_t *)in;
        len                          = sizeof(bModbusWriteRegsAck_t);
        if (i_len < len)
        {
            return -1;
        }
        param.slave_id  = w_ack->addr;
        param.func_code = w_ack->func;
        param.base_reg  = L2B_B2L_16b(w_ack->reg);
        param.reg_num   = L2B_B2L_16b(w_ack->num);
        param.reg_value = NULL;
        B_SAFE_INVOKE(pattr->callback, B_MODBUS_CMD_WRITE_REG, &param);
    }

    return 0;
}

static int _bModbusRTUPackage(void *attr, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len)
{
    int      i   = 0;
    int      len = 0;
    uint16_t crc = 0;
    if (buf == NULL || buf_len == 0)
    {
        return -1;
    }

    if (cmd == B_MODBUS_CMD_READ_REG)
    {
        bModbusRead_t *param = (bModbusRead_t *)buf;
        len                  = sizeof(bModbusRead_t) + 2;
        if (buf_len < len)
        {
            return -1;
        }
        param->base_reg = L2B_B2L_16b(param->base_reg);
        param->reg_num  = L2B_B2L_16b(param->reg_num);
        param->reserved = MODBUS_RTU_READ_REGS;
    }
    else if (cmd == B_MODBUS_CMD_WRITE_REG)
    {
        bModbusWrite_t *param = (bModbusWrite_t *)buf;
        if (param->reg_num == 0)
        {
            return -1;
        }
        if (param->reg_num == 1)
        {
            len = sizeof(bModbusWriteReg_t);
        }
        else
        {
            len = sizeof(bModbusWriteRegs_t) + param->reg_num * 2;
        }
        if (buf_len < len)
        {
            return -1;
        }
        param->base_reg = L2B_B2L_16b(param->base_reg);
        if (param->reg_num == 1)
        {
            bModbusWriteReg_t *frame = (bModbusWriteReg_t *)buf;
            frame->func              = MODBUS_RTU_WRITE_REG;
            frame->value             = L2B_B2L_16b(param->reg_value[0]);
        }
        else
        {
            bModbusWriteRegs_t *frame = (bModbusWriteRegs_t *)buf;
            frame->len                = param->reg_num * 2;
            frame->func               = MODBUS_RTU_WRITE_REGS;
            param->reg_num            = L2B_B2L_16b(param->reg_num);
            for (i = 0; i < frame->len / 2; i++)
            {
                frame->param[i] = L2B_B2L_16b((param->reg_value)[i]);
            }
        }
    }

    if (len > 0)
    {
        crc          = _bMBCRC16(buf, len - 2);
        buf[len - 2] = ((crc >> 0) & 0xff);
        buf[len - 1] = ((crc >> 8) & 0xff);
    }

    return len;
}

/**
 * \}
 */

/**
 * \addtogroup MODBUS_Exported_Functions
 * \{
 */

bPROTOCOL_REG_INSTANCE("modbus", _bModbusRTUParse, _bModbusRTUPackage);

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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
