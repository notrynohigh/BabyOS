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
} bModbusMasterSendReadRegs_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint8_t  len;
    uint16_t param[1];
} bModbusMasterSendReadRegsAck_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint8_t  len;
    uint16_t param[1];
} bModbusMasterSendWriteRegs_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;    // Big endian
    uint16_t value;  // Big endian
    uint16_t crc;    // Little endian
} bModbusMasterSendWriteReg_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint16_t crc;  // Little endian
} bModbusMasterSendWriteRegsAck_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;    // Big endian
    uint16_t value;  // Big endian
    uint16_t crc;    // Little endian
} bModbusMasterSendWriteRegAck_t;

//----------------------------------------------------------------
typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint16_t crc;  // Little endian
} bModbusSlaveRecvReadRegs_t;

typedef struct
{
    uint8_t addr;
    uint8_t func;
    uint8_t len;
    uint8_t buf[1];
} bModbusSlaveRecvReadRegsAck_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint8_t  len;
    uint16_t param[1];
} bModbusSlaveRecvWriteRegs_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;    // Big endian
    uint16_t value;  // Big endian
    uint16_t crc;    // Little endian
} bModbusSlaveRecvWriteReg_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;  // Big endian
    uint16_t num;  // Big endian
    uint16_t crc;  // Little endian
} bModbusSlaveRecvWriteRegsAck_t;

typedef struct
{
    uint8_t  addr;
    uint8_t  func;
    uint16_t reg;    // Big endian
    uint16_t value;  // Big endian
    uint16_t crc;    // Little endian
} bModbusSlaveRecvWriteRegAck_t;

#pragma pack()
/**
 * \}
 */

/**
 * \defgroup MODBUS_Private_Defines
 * \{
 */
#define MODBUS_READ_REG_MAX 125
#define MODBUS_WRITE_REG_MAX 123
#define ALL_SLAVE_ADDR 0XFF
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

/**
 * @description:
 * 主机收到的MODBUS帧解析是否正确?即从机的发送,此为原始的大端输入,解析正确后转换为小端,调用回调来更新寄存器表
 * @param {void} *attr
 * @param {uint8_t} *in 收到的从机应答帧的头
 * @param {uint16_t} i_len 收到的从机应答帧长度
 * @param {uint8_t} *out
 * @param {uint16_t} o_len
 * @return {*} 0:正常 <0:不正常
 */
static int _bModbusRTUMasterParse(void *attr, uint8_t *in, uint16_t i_len, uint8_t *out,
                                  uint16_t o_len)
{
    int              retval = 0;
    int              len    = 0;
    int              i      = 0;
    uint16_t         crc    = 0;
    bProtocolAttr_t *pattr  = (bProtocolAttr_t *)attr;
    bModbusCbParm_t  param;

    if (i_len < 2)
    {
        return MODBUS_LEN_ERR;
    }

    if (in[1] == MODBUS_RTU_READ_REGS)
    {
        bModbusMasterSendReadRegsAck_t *r_ack = (bModbusMasterSendReadRegsAck_t *)in;
        len                                   = sizeof(bModbusMasterSendReadRegsAck_t) + r_ack->len;
        if (i_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        crc = _bMBCRC16(in, len - 2);
        if (crc != r_ack->param[r_ack->len / 2])
        {
            return MODBUS_FRAME_HEAD_ERR;
        }
        param.slave_id = pattr->reserved;  // 用户层保证该地址为从机的实际设备地址
        param.func_code = r_ack->func;
        param.base_reg  = 0;
        param.reg_num   = r_ack->len / 2;
        for (i = 0; i < param.reg_num; i++)
        {
            r_ack->param[i] = L2B_B2L_16b(r_ack->param[i]);
        }
        param.reg_value = (uint16_t *)r_ack->param;
        B_SAFE_INVOKE_RET(retval, pattr->callback, B_MODBUS_CMD_READ_REG, &param, pattr->arg);
        if ((retval < 0) && (pattr->callback != NULL))
        {
            return MODBUS_CALLBACK_ERR;
        }
    }
    else if (in[1] == MODBUS_RTU_WRITE_REG)
    {
        bModbusMasterSendWriteRegAck_t *w_1_ack = (bModbusMasterSendWriteRegAck_t *)in;
        len                                     = sizeof(bModbusMasterSendWriteRegAck_t);
        if (i_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        crc = _bMBCRC16(in, len - 2);
        if (crc != w_1_ack->crc)
        {
            return MODBUS_FRAME_HEAD_ERR;
        }
        param.slave_id = pattr->reserved;  // 用户层保证该地址为从机的实际设备地址
        param.func_code = w_1_ack->func;
        param.base_reg  = L2B_B2L_16b(w_1_ack->reg);
        param.reg_num   = 1;
        w_1_ack->value  = L2B_B2L_16b(w_1_ack->value);
        param.reg_value = (uint16_t *)&w_1_ack->value;
        B_SAFE_INVOKE_RET(retval, pattr->callback, B_MODBUS_CMD_WRITE_REGS, &param, pattr->arg);
        if ((retval < 0) && (pattr->callback != NULL))
        {
            return MODBUS_CALLBACK_ERR;
        }
    }
    else if (in[1] == MODBUS_RTU_WRITE_REGS)
    {
        bModbusMasterSendWriteRegsAck_t *w_ack = (bModbusMasterSendWriteRegsAck_t *)in;
        len                                    = sizeof(bModbusMasterSendWriteRegsAck_t);
        if (i_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        crc = _bMBCRC16(in, len - 2);
        if (crc != w_ack->crc)
        {
            return MODBUS_FRAME_HEAD_ERR;
        }
        param.slave_id = pattr->reserved;  // 用户层保证该地址为从机的实际设备地址
        param.func_code = w_ack->func;
        param.base_reg  = L2B_B2L_16b(w_ack->reg);
        param.reg_num   = L2B_B2L_16b(w_ack->num);
        param.reg_value = NULL;
        B_SAFE_INVOKE_RET(retval, pattr->callback, B_MODBUS_CMD_WRITE_REGS, &param, pattr->arg);
        if ((retval < 0) && (pattr->callback != NULL))
        {
            return MODBUS_CALLBACK_ERR;
        }
    }
    else
    {
        return MODBUS_CRC_ERR;
    }

    return 0;
}

/**
 * @description:
 * @param {void} *attr
 * @param {bProtoCmd_t} cmd
 * @param {uint8_t} *buf
 * 该数组同时将读写的参数按照小端传入,具体是什么数据类型,依据CMD来决定,获取后重新按照大端组帧
 * @param {uint16_t} buf_len 长度需要至少和相应CMD的数据结构大小相等
 * @return {*}
 */
static int _bModbusRTUMasterPackage(void *attr, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len)
{
    int      i   = 0;
    int      len = 0;
    uint16_t crc = 0;
    if (buf == NULL || buf_len == 0)
    {
        return MODBUS_LEN_ERR;
    }

    if (cmd == B_MODBUS_CMD_READ_REG)
    {
        bModbusMasterRead_t *param = (bModbusMasterRead_t *)buf;
        len                        = sizeof(bModbusMasterRead_t) + 2;
        if (buf_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        param->base_reg = L2B_B2L_16b(param->base_reg);
        param->reg_num  = L2B_B2L_16b(param->reg_num);
        param->reserved = MODBUS_RTU_READ_REGS;
    }
    else if (cmd == B_MODBUS_CMD_WRITE_REG)
    {
        len = sizeof(bModbusMasterSendWriteReg_t);
        if (buf_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        bModbusMasterSendWriteReg_t *frame = (bModbusMasterSendWriteReg_t *)buf;
        frame->func                        = MODBUS_RTU_WRITE_REG;
        frame->reg                         = L2B_B2L_16b(frame->reg);
        frame->value                       = L2B_B2L_16b(frame->value);
    }
    else if (cmd == B_MODBUS_CMD_WRITE_REGS)
    {
        bModbusMasterWriteRegs_t *param = (bModbusMasterWriteRegs_t *)buf;
        len                             = sizeof(bModbusMasterWriteRegs_t) + param->reg_num * 2;
        if (buf_len < len)
        {
            return MODBUS_LEN_ERR;
        }

        bModbusMasterSendWriteRegs_t *frame = (bModbusMasterSendWriteRegs_t *)buf;
        frame->len                          = param->reg_num * 2;
        frame->func                         = MODBUS_RTU_WRITE_REGS;
        frame->reg                          = L2B_B2L_16b(param->base_reg);
        frame->num                          = L2B_B2L_16b(param->reg_num);
        for (i = 0; i < frame->len / 2; i++)
        {
            frame->param[i] = L2B_B2L_16b(frame->param[i]);
        }
    }
    else
    {
        return MODBUS_FRAME_HEAD_ERR;
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
 * @description:
 * 从机收到的MODBUS帧解析是否正确?即主机的发送,此为原始的大端输入,解析正确后转换为小端,调用回调来更新寄存器表
 * @param {void} *attr
 * @param {uint8_t} *in
 * @param {uint16_t} i_len
 * @param {uint8_t} *out
 * @param {uint16_t} o_len
 * @return {*} -1长度不对 -2帧头错误 -3校验错误 -4格式错误 -5操作非法地址
 */
static int _bModbusRTUSlaveParse(void *attr, uint8_t *in, uint16_t i_len, uint8_t *out,
                                 uint16_t o_len)
{
    int              retval = 0;
    int              len    = 0;
    uint16_t         crc    = 0;
    bProtocolAttr_t *pattr  = (bProtocolAttr_t *)attr;
    bModbusCbParm_t  param;
    bModbusInf_t     modbus_inf;

    if (i_len < 2)
    {
        return MODBUS_LEN_ERR;
    }

    if ((in[0] != pattr->reserved) && (in[0] != ALL_SLAVE_ADDR))
    {
        return MODBUS_FRAME_HEAD_ERR;
    }

    memset(&modbus_inf, 0, sizeof(bModbusInf_t));

    if (in[1] == MODBUS_RTU_READ_REGS)
    {
        bModbusSlaveRecvReadRegs_t *r = (bModbusSlaveRecvReadRegs_t *)in;
        len                           = sizeof(bModbusSlaveRecvReadRegs_t);
        if (i_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        crc = _bMBCRC16(in, len - 2);
        if (crc != r->crc)
        {
            return MODBUS_CRC_ERR;
        }
        param.slave_id = pattr->reserved;  // 用户层保证该地址为从机的实际设备地址
        param.func_code = r->func;
        param.base_reg  = L2B_B2L_16b(r->reg);
        param.reg_num   = L2B_B2L_16b(r->num);
        if (param.reg_num > MODBUS_READ_REG_MAX)
        {
            return MODBUS_MAX_REGNUM_ERR;
        }
        if ((param.base_reg >= MY_DEVICE_MODBUS_REG_NUM) ||
            ((param.base_reg + param.reg_num) >= MY_DEVICE_MODBUS_REG_NUM + 1))
        {
            return MODBUS_ILLEGAL_REG_ERR;
        }
        param.reg_value = NULL;

        B_SAFE_INVOKE_RET(retval, pattr->get_info, B_PROTO_INFO_MODBUS_REG_PERMISSION,
                          (uint8_t *)&modbus_inf, sizeof(bModbusInf_t));
        if ((retval >= 0) && (pattr->get_info != NULL))
        {
            // 判定待操作的寄存器指定动作与读写权限匹配?
            for (uint32_t i = param.base_reg; i <= param.base_reg + param.reg_num - 1; i++)
            {
                if (modbus_inf.ArrayPtr[i][0] == 0)
                {
                    return MODBUS_REG_OPERATION_ERR;
                }
            }
        }

        B_SAFE_INVOKE_RET(retval, pattr->callback, B_MODBUS_CMD_READ_REG, &param, pattr->arg);
        if ((retval < 0) && (pattr->callback != NULL))
        {
            return MODBUS_CALLBACK_ERR;
        }
    }
    else if (in[1] == MODBUS_RTU_WRITE_REG)
    {
        bModbusSlaveRecvWriteReg_t *w_1 = (bModbusSlaveRecvWriteReg_t *)in;
        len                             = sizeof(bModbusSlaveRecvWriteReg_t);
        if (i_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        crc = _bMBCRC16(in, len - 2);
        if (crc != w_1->crc)
        {
            return MODBUS_CRC_ERR;
        }
        param.slave_id = pattr->reserved;  // 用户层保证该地址为从机的实际设备地址
        param.func_code = w_1->func;
        param.base_reg  = L2B_B2L_16b(w_1->reg);
        param.reg_num   = 1;
        if (param.base_reg >= MY_DEVICE_MODBUS_REG_NUM)
        {
            return MODBUS_ILLEGAL_REG_ERR;
        }
        w_1->value      = L2B_B2L_16b(w_1->value);
        param.reg_value = &w_1->value;

        B_SAFE_INVOKE_RET(retval, pattr->get_info, B_PROTO_INFO_MODBUS_REG_PERMISSION,
                          (uint8_t *)&modbus_inf, sizeof(bModbusInf_t));
        if ((retval >= 0) && (pattr->get_info != NULL))
        {
            // 判定待操作的寄存器指定动作与读写权限匹配?
            for (uint32_t i = param.base_reg; i <= param.base_reg + param.reg_num - 1; i++)
            {
                if (modbus_inf.ArrayPtr[i][1] == 0)
                {
                    return MODBUS_REG_OPERATION_ERR;
                }
            }
        }

        B_SAFE_INVOKE_RET(retval, pattr->callback, B_MODBUS_CMD_WRITE_REGS, &param, pattr->arg);
        if ((retval < 0) && (pattr->callback != NULL))
        {
            return MODBUS_CALLBACK_ERR;
        }
    }
    else if (in[1] == MODBUS_RTU_WRITE_REGS)
    {
        bModbusSlaveRecvWriteRegs_t *w = (bModbusSlaveRecvWriteRegs_t *)in;
        len                            = sizeof(bModbusSlaveRecvWriteRegs_t) + w->len;
        if (i_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        crc = _bMBCRC16(in, len - 2);
        if (crc != w->param[w->len / 2])
        {
            return MODBUS_CRC_ERR;
        }
        param.slave_id = pattr->reserved;  // 用户层保证该地址为从机的实际设备地址
        param.func_code = w->func;
        param.base_reg  = L2B_B2L_16b(w->reg);
        param.reg_num   = L2B_B2L_16b(w->num);
        if (param.reg_num > MODBUS_WRITE_REG_MAX)
        {
            return MODBUS_MAX_REGNUM_ERR;
        }
        if ((param.reg_num * 2) != w->len)
        {
            return MODBUS_MAX_REGNUM_ERR;
        }
        if ((param.base_reg >= MY_DEVICE_MODBUS_REG_NUM) ||
            ((param.base_reg + param.reg_num) >= MY_DEVICE_MODBUS_REG_NUM + 1))
        {
            return MODBUS_ILLEGAL_REG_ERR;
        }
        for (uint16_t i = 0; i < param.reg_num; i++)
        {
            (w->param)[i] = L2B_B2L_16b((w->param)[i]);
        }
        param.reg_value = w->param;

        B_SAFE_INVOKE_RET(retval, pattr->get_info, B_PROTO_INFO_MODBUS_REG_PERMISSION,
                          (uint8_t *)&modbus_inf, sizeof(bModbusInf_t));
        if ((retval >= 0) && (pattr->get_info != NULL))
        {
            // 判定待操作的寄存器指定动作与读写权限匹配?
            for (uint32_t i = param.base_reg; i <= param.base_reg + param.reg_num - 1; i++)
            {
                if (modbus_inf.ArrayPtr[i][1] == 0)
                {
                    return MODBUS_REG_OPERATION_ERR;
                }
            }
        }

        B_SAFE_INVOKE_RET(retval, pattr->callback, B_MODBUS_CMD_WRITE_REGS, &param, pattr->arg);
        if ((retval < 0) && (pattr->callback != NULL))
        {
            return MODBUS_CALLBACK_ERR;
        }
    }
    else
    {
        return MODBUS_MAX_REGNUM_ERR;
    }

    return 0;
}

/**
 * @description: 将信息除去校验位按照位置还有小端存在buf内,该函数会判定格式后大小端转换
 * @param {void} *attr
 * @param {bProtoCmd_t} cmd
 * @param {uint8_t} *buf
 * 该数组同时将读写的参数按照小端传入,具体是什么数据类型,依据CMD来决定,判定帧格式成功后重新按照大端组帧
 * @param {uint16_t} buf_len
 * @return {*} -1长度不对 -2帧头错误 -3校验错误 -4格式错误 -5操作非法地址 -6调用错误
 */
static int _bModbusRTUSlavePackage(void *attr, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len)
{
    uint16_t         i     = 0;
    int              len   = 0;
    uint16_t         crc   = 0;
    bProtocolAttr_t *pattr = (bProtocolAttr_t *)attr;

    if (buf == NULL || buf_len == 0)
    {
        return MODBUS_LEN_ERR;
    }

    if ((buf[0] != pattr->reserved) && (buf[0] == ALL_SLAVE_ADDR))
    {
        return MODBUS_FRAME_HEAD_ERR;
    }

    if (cmd == B_MODBUS_CMD_READ_REG)
    {
        bModbusSlaveRead_t *param = (bModbusSlaveRead_t *)buf;
        len                       = sizeof(bModbusSlaveRead_t) + param->len;
        if (buf_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        if (param->reserved != MODBUS_RTU_READ_REGS)
        {
            return MODBUS_MAX_REGNUM_ERR;
        }
        if ((param->len % 2 != 0) && (param->len != 0))
        {
            return MODBUS_LEN_ERR;
        }
        for (i = 0; i < param->len / 2; i++)
        {
            param->reg_value[i] = L2B_B2L_16b(param->reg_value[i]);
        }
    }
    else if (cmd == B_MODBUS_CMD_WRITE_REG)
    {
        bModbusSlaveWriteReg_t *frame = (bModbusSlaveWriteReg_t *)buf;
        len                           = sizeof(bModbusSlaveWriteReg_t);
        if (buf_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        if (frame->reserved != MODBUS_RTU_WRITE_REG)
        {
            return MODBUS_MAX_REGNUM_ERR;
        }
        if (frame->base_reg >= MY_DEVICE_MODBUS_REG_NUM)
        {
            return MODBUS_ILLEGAL_REG_ERR;
        }
        frame->base_reg  = L2B_B2L_16b(frame->base_reg);
        frame->reg_value = L2B_B2L_16b(frame->reg_value);
    }
    else if (cmd == B_MODBUS_CMD_WRITE_REGS)
    {
        bModbusSlaveWriteRegs_t *frame = (bModbusSlaveWriteRegs_t *)buf;
        len                            = sizeof(bModbusSlaveWriteRegs_t);
        if (buf_len < len)
        {
            return MODBUS_LEN_ERR;
        }
        if (frame->reserved != MODBUS_RTU_WRITE_REGS)
        {
            return MODBUS_MAX_REGNUM_ERR;
        }
        if ((frame->base_reg >= MY_DEVICE_MODBUS_REG_NUM) ||
            ((frame->base_reg + frame->reg_num) >= MY_DEVICE_MODBUS_REG_NUM + 1))
        {
            return MODBUS_ILLEGAL_REG_ERR;
        }
        frame->base_reg = L2B_B2L_16b(frame->base_reg);
        frame->reg_num  = L2B_B2L_16b(frame->reg_num);
    }
    else
    {
        return MODBUS_CALLBACK_ERR;
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
#ifdef BSECTION_NEED_PRAGMA
#pragma section b_srv_protocol
#endif
bPROTOCOL_REG_INSTANCE("modbus_master", _bModbusRTUMasterParse, _bModbusRTUMasterPackage);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif

#ifdef BSECTION_NEED_PRAGMA
#pragma section b_srv_protocol
#endif
bPROTOCOL_REG_INSTANCE("modbus_slave", _bModbusRTUSlaveParse, _bModbusRTUSlavePackage);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif
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
