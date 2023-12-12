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
#define COLS 2
typedef int (*ArrayPtr)[COLS];
typedef struct
{
    uint32_t Reg_Rows;  // Modbus读写权限表中二维数组的行=从机寄存器的个数
    uint32_t Cols;  // Modbus读写权限表中二维数组的列,第一个元素为读,第二个元素为写
    uint16_t (*ArrayPtr)[COLS];  // 一维数组指针，指向二维数组第一行元素
} bModbusInf_t;

typedef enum
{
    MODBUS_LEN_ERR           = -1,
    MODBUS_FRAME_HEAD_ERR    = -2,
    MODBUS_CRC_ERR           = -3,
    MODBUS_MAX_REGNUM_ERR    = -4,  // 超过一帧能放的下寄存器数
    MODBUS_ILLEGAL_REG_ERR   = -5,  // 操作非法寄存器
    MODBUS_REG_OPERATION_ERR = -6,  // 无操作寄存器权限
    MODBUS_CALLBACK_ERR      = -7,  // 回调告知错误

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
#define L2B_B2L_16b(n) ((((n)&0xff) << 8) | (((n)&0xff00) >> 8))
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
