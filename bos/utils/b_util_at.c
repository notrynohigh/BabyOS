/**
 *!
 * \file        b_mod_at.c
 * \version     v0.0.2
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

/*Includes ----------------------------------------------*/
#include "utils/inc/b_util_at.h"
#if (defined(_AT_ENABLE) && (_AT_ENABLE == 1))
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

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
 * \defgroup AT_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AT_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AT_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AT_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AT_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AT_Private_Functions
 * \{
 */

PT_THREAD(bAtTask)(struct pt *pt, void *arg)
{
    uint8_t      result = 0;
    bAtStruct_t *pat    = (bAtStruct_t *)arg;
    PT_BEGIN(pt);
    while (1)
    {
        PT_WAIT_UNTIL_FOREVER(pt, pat->pcmd != NULL);
        pat->send((const uint8_t *)pat->pcmd, strlen(pat->pcmd), pat->user_data);
        pat->pcmd = NULL;
        if (pat->resp == NULL)
        {
            pat->cmd_cb(1, pat->user_data);
        }
        else
        {
            b_log("wait resp %dms..\r\n", pat->timeout);
            PT_WAIT_UNTIL(pt, pat->resp == NULL, pat->timeout);
            if (pat->resp == NULL)
            {
                result = 1;
            }
            pat->resp    = NULL;
            pat->pcmd    = NULL;
            pat->timeout = 0;
            pat->cmd_cb(result, pat->user_data);
        }
    }
    PT_END(pt);
}

static void _bAtFree(void *arg)
{
    bFree(arg);
}

/**
 * \}
 */

/**
 * \addtogroup AT_Exported_Functions
 * \{
 */

int bAtInit(bAtStruct_t *pat, pAtCmdCb_t cmd_cb, pAtNewDataCb_t data_cb, pAtSendData_t send,
            void *user_data)
{
    if (pat == NULL || cmd_cb == NULL || data_cb == NULL)
    {
        return -1;
    }
    b_log("at init..\r\n");
    memset(pat, 0, sizeof(bAtStruct_t));
    pat->cmd_cb    = cmd_cb;
    pat->data_cb   = data_cb;
    pat->send      = send;
    pat->pcmd      = NULL;
    pat->user_data = user_data;
    bTaskCreate("at", bAtTask, pat, &pat->attr);
    return 0;
}

int bAtSendCmd(bAtStruct_t *pat, const char *pcmd, const char *resp, uint16_t timeout)
{
    b_log("atcmd: %s\r\n", pcmd);
    if (pat == NULL || pcmd == NULL || pat->cmd_cb == NULL || pat->data_cb == NULL ||
        pat->send == NULL)
    {
        b_log_e("param error.%p,%p,%p,%p,%p.\r\n", pat, pcmd, pat->cmd_cb, pat->data_cb, pat->send);
        return -1;
    }
    if (resp != NULL && timeout == 0)
    {
        b_log_e("resp param error..\r\n");
        return -1;
    }
    if (pat->pcmd != NULL || pat->resp != NULL)
    {
        b_log_e("busy..\r\n");
        return -2;
    }
    pat->pcmd    = pcmd;
    pat->resp    = resp;
    pat->timeout = timeout;
    return 0;
}

int bAtFeedData(bAtStruct_t *pat, uint8_t *pbuf, uint16_t len)
{
    if (pat == NULL || pbuf == NULL)
    {
        return -1;
    }
    b_log(">[%d]%s\r\n", len, pbuf);
    if (pat->resp)
    {
        b_log("resp[%s]\r\n", pat->resp);
        if (strstr((const char *)pbuf, pat->resp) != NULL)
        {
            b_log("match ok..\r\n");
            pat->resp = NULL;
        }
    }
    else
    {
        uint8_t *p = (uint8_t *)bMalloc(len);
        if (p == NULL)
        {
            return -1;
        }
        memcpy(p, pbuf, len);
        pat->data_cb(p, len, _bAtFree, pat->user_data);
    }
    return 0;
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

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
