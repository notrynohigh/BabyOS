/**
 *!
 * \file        b_util_at.h
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
#ifndef __B_UTIL_AT_H__
#define __B_UTIL_AT_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "b_config.h"

#if (defined(_AT_ENABLE) && (_AT_ENABLE == 1))

#include "core/inc/b_task.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup B_UTILS
 * \{
 */

/**
 * \addtogroup AT
 * \{
 */

/**
 * \defgroup AT_Exported_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AT_Exported_TypesDefinitions
 * \{
 */

typedef void (*pAtCmdCb_t)(uint8_t isok, void *user_data);
typedef void (*pAtNewDataCb_t)(uint8_t *pbuf, uint16_t len, void (*pfree)(void *), void *user_data);
typedef void (*pAtSendData_t)(const uint8_t *pbuf, uint16_t len, void *user_data);

typedef struct
{
    const char    *pcmd;
    const char    *resp;
    void          *user_data;
    uint16_t       timeout;
    pAtCmdCb_t     cmd_cb;
    pAtNewDataCb_t data_cb;
    pAtSendData_t  send;
    bTaskAttr_t    attr;
} bAtStruct_t;

/**
 * \}
 */

/**
 * \defgroup AT_Exported_Functions
 * \{
 */

int bAtInit(bAtStruct_t *pat, pAtCmdCb_t cmd_cb, pAtNewDataCb_t data_cb, pAtSendData_t send,
            void *user_data);
int bAtSendCmd(bAtStruct_t *pat, const char *pcmd, const char *resp, uint16_t timeout);
int bAtFeedData(bAtStruct_t *pat, uint8_t *pbuf, uint16_t len);
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
