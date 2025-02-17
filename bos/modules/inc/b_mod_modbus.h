/**
 *!
 * \file        b_mod_modbus.h
 * \version     v0.0.1
 * \date        2019/06/05
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
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
#ifndef __B_MOD_MODBUS_H__
#define __B_MOD_MODBUS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_MODBUS_ENABLE) && (_MODBUS_ENABLE == 1))

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
 * \addtogroup MODBUS
 * \{
 */

/**
 * \defgroup MODBUS_Exported_TypesDefinitions
 * \{
 */

#ifndef MY_DEVICE_MODBUS_REG_NUM
#define MY_DEVICE_MODBUS_REG_NUM 1
#endif

// 每2bit表示1个寄存器的读写权限
// 00：禁止读写  01:可读  10:可写  11:可读写
// 使用 MODBUS_PERM_ 开头的宏方便创建表和使用表
typedef struct
{
    uint8_t perm[(((MY_DEVICE_MODBUS_REG_NUM * 2) + 7) / 8)];
} bModbusPerm_t;

typedef enum
{
    MODBUS_LEN_ERR           = -1,
    MODBUS_FRAME_HEAD_ERR    = -2,
    MODBUS_CRC_ERR           = -3,
    MODBUS_MAX_REGNUM_ERR    = -4,  // 超过一帧能放的下寄存器数
    MODBUS_ILLEGAL_REG_ERR   = -5,  // 操作非法寄存器
    MODBUS_REG_OPERATION_ERR = -6,  // 无操作寄存器权限
    MODBUS_CALLBACK_ERR      = -7,  // 回调告知错误
    MODBUS_GET_REGVALUE_ERR  = -8,  // 无法获取从机寄存器值
} bModbusErrCode_t;

/**
 * \}
 */

/**
 * \defgroup MODBUS_Exported_Defines
 * \{
 */
#define MODBUS_RTU_READ_REGS (0x3)
#define MODBUS_RTU_WRITE_REGS (0x10)
#define MODBUS_RTU_WRITE_REG (0x06)
/**
 * \}
 */

/**
 * \defgroup MODBUS_Exported_Macros
 * \{
 */
#define L2B_B2L_16b(n) ((((n) & 0xff) << 8) | (((n) & 0xff00) >> 8))

#define MODBUS_PERM_NONE 0x00       // 00
#define MODBUS_PERM_READABLE 0x01   // 01
#define MODBUS_PERM_WRITABLE 0x02   // 10
#define MODBUS_PERM_READWRITE 0x03  // 11
#define MODBUS_PERM_CREATE_TABLE(name) static bModbusPerm_t name = {0}
#define MODBUS_PERM_CREATE_SET_STATE(p_perm, reg, state)           \
    do                                                             \
    {                                                              \
        uint8_t mask = 0x03;                                       \
        (p_perm)->perm[(reg) / 4] &= ~(mask << (((reg) % 4) * 2)); \
        (p_perm)->perm[(reg) / 4] |= (state << (((reg) % 4) * 2)); \
    } while (0)

#define MODBUS_PERM_IS_WRITEABLE(p_perm, reg) \
    (((p_perm)->perm[(reg) / 4] >> (((reg) % 4) * 2)) & MODBUS_PERM_WRITABLE)
#define MODBUS_PERM_IS_READABLE(p_perm, reg) \
    (((p_perm)->perm[(reg) / 4] >> (((reg) % 4) * 2)) & MODBUS_PERM_READABLE)

/**
 * \}
 */

/**
 * \defgroup MODBUS_Exported_Functions
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
