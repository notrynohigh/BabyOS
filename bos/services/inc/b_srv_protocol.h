/**
 *!
 * \file        b_srv_protocol.h
 * \version     v0.0.1
 * \date        2023/08/27
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 Bean
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
#ifndef __B_SRV_PROTOCOL_H__
#define __B_SRV_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_PROTOCOL_SERVICE_ENABLE) && (_PROTOCOL_SERVICE_ENABLE == 1))

#include "modules/inc/b_mod_proto_type.h"
#include "utils/inc/b_util_list.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
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

typedef void *bProtSrvId_t;

typedef struct
{
    const char     *name;
    bProtocolAttr_t attr;
} bProtSrvAttr_t;

typedef int (*bProtSrvCallback_t)(bProtoCmd_t cmd, void *param);
typedef int (*bProtSrvGetInfo_t)(bProtoInfoType_t type, uint8_t *buf, uint16_t buf_len);

typedef struct
{
    uint8_t            number;
    const bProtoCmd_t *pcmd_table;
    bProtSrvCallback_t callback;
    struct list_head   list;
} bProtSrvSubscribe_t;

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Exported_Defines
 * \{
 */

// proto_name \ref "bos": babyos私有协议
//                 "modbus", "xmodem128", "ymodem"
// callback \ref bProtSrvCallback_t
#define B_PROT_SRV_CREATE_ATTR(attr_name, proto_name, _callback) \
    static bProtSrvAttr_t attr_name = {                          \
        .name            = proto_name,                           \
        .attr.u_callback = _callback,                            \
        .attr.parse      = NULL,                                 \
        .attr.package    = NULL,                                 \
    }

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Exported_Functions
 * \{
 */
// attr \ref B_PROT_SRV_CREATE_ATTR
// pfunc 提供函数给协议层获取信息
bProtSrvId_t bProtSrvInit(bProtSrvAttr_t *attr, bProtSrvGetInfo_t func);

// in/i_len 需要解析的数据和数据长度
// out/o_len 已经组包完成，需要回复的数据
// retval 返回值是，out中的有效数据长度。-1表示打包失败
int bProtSrvParse(bProtSrvId_t id, uint8_t *in, uint16_t i_len, uint8_t *out, uint16_t o_len);

// cmd 是需要打包什么数据 \ref bProtoCmd_t
// buf 打包后的数据存放区
// buf_len buf的长度
// retval buf中有效数据长度，-1表示打包失败
int bProtSrvPackage(bProtSrvId_t id, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len);

// 订阅协议的指令
// psub 订阅信息 \ref bProtSrvSubscribe_t
int bProtSrvSubscribe(bProtSrvId_t id, bProtSrvSubscribe_t *psub);

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

/************************ Copyright (c) 2023 Bean *****END OF FILE****/
