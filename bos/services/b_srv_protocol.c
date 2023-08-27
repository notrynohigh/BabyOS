/**
 *!
 * \file        b_srv_protocol.c
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

/*Includes ----------------------------------------------*/
#include "services/inc/b_srv_protocol.h"

#if (defined(_PROTOCOL_SERVICE_ENABLE) && (_PROTOCOL_SERVICE_ENABLE == 1))

#include <string.h>

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
 * \defgroup PROTOCOL_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Defines
 * \{
 */

bSECTION_DEF_FLASH(b_srv_protocol, bProtocolInstance_t);

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Functions
 * \{
 */

/**
 * \}
 */

/**
 * \addtogroup PROTOCOL_Exported_Functions
 * \{
 */

bProtSrvId_t bProtSrvInit(bProtSrvAttr_t *attr, bProtSrvGetInfo_t func)
{
    if (attr == NULL)
    {
        return NULL;
    }
    bSECTION_FOR_EACH(b_srv_protocol, bProtocolInstance_t, instance)
    {
        if (strcmp(instance->name, attr->name) == 0)
        {
            attr->attr.get_info = func;
            attr->attr.package  = instance->package;
            attr->attr.parse    = instance->parse;
            break;
        }
    }
    if (attr->attr.parse && attr->attr.package)
    {
        return attr;
    }
    return NULL;
}

// in/i_len 需要解析的数据和数据长度
// out/o_len 已经组包完成，需要回复的数据
// retval 返回值是，out中的有效数据长度。-1表示打包失败
int bProtSrvParse(bProtSrvId_t id, uint8_t *in, uint8_t i_len, uint8_t *out, uint16_t o_len)
{
    bProtSrvAttr_t *pattr = (bProtSrvAttr_t *)id;
    if (pattr == NULL || in == NULL)
    {
        return -1;
    }
    return pattr->attr.parse(&pattr->attr, in, i_len, out, o_len);
}

// cmd 是需要打包什么数据 \ref bProtoCmd_t
// buf 打包后的数据存放区
// buf_len buf的长度
// retval buf中有效数据长度，-1表示打包失败
int bProtSrvPackage(bProtSrvId_t id, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len)
{
    bProtSrvAttr_t *pattr = (bProtSrvAttr_t *)id;
    if (pattr == NULL || buf == NULL || cmd >= B_PROTO_CMD_NUMBER)
    {
        return -1;
    }
    return pattr->attr.package(&pattr->attr, cmd, buf, buf_len);
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

/************************ Copyright (c) 2023 Bean *****END OF FILE****/
