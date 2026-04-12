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
#include "utils/inc/b_util_tools.h"

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

static void _bAtReset(bAtStruct_t *pat)
{
    pat->resp    = NULL;
    pat->pcmd    = NULL;
    pat->timeout = 0;
}

static void _bAtFree(void *arg)
{
    bFree(arg);
}

static int _bAtCheckResp(bAtStruct_t *pat, const char *resp)
{
    const char *pat_resp = pat->resp;
    uint8_t     i = 0, j = 0;
    int32_t     tmp            = 0;
    const char *pstr           = NULL;
    char        strbuf[32 + 1] = {0};
    while (0 == bParseString(pat_resp, ",", i, &tmp, &pstr))
    {
        for (j = 0; j < 32; j++)
        {
            if (pstr[j] != ',' && pstr[j] != '\0')
            {
                strbuf[j] = pstr[j];
            }
            else
            {
                strbuf[j] = '\0';
                break;
            }
        }
        if (j >= 32)
        {
            return -1;
        }
        if (strstr(resp, strbuf) != NULL)
        {
            return 0;
        }
        i += 1;
    }
    return -1;
}

PT_THREAD(bAtTask)(struct pt *pt, void *arg)
{
    uint8_t      result = 0;
    bAtStruct_t *pat    = (bAtStruct_t *)arg;
    PT_BEGIN(pt);
    while (1)
    {
#if _AT_DYNAMIC_ENABLE
        PT_WAIT_UNTIL_FOREVER(pt, (pat->pcmd != NULL || pat->recv_resp_data != NULL));
#else
        PT_WAIT_UNTIL_FOREVER(pt, (pat->pcmd != NULL));
#endif
        if (pat->pcmd != NULL)
        {
            pat->send((const uint8_t *)pat->pcmd, pat->cmd_len, pat->user_data);
            pat->pcmd = NULL;
            if (pat->resp == NULL)
            {
                _bAtReset(pat);
                B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_OK, NULL, 0, NULL, pat->user_data);
            }
            else
            {
                b_log("wait resp %dms..\r\n", pat->timeout);
#if _AT_DYNAMIC_ENABLE
                PT_WAIT_UNTIL(pt, pat->recv_resp_data != NULL, pat->timeout);
                _bAtReset(pat);
                if (PT_WAIT_IS_TIMEOUT(pt))
                {
                    B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_TIMEOUT, NULL, 0, NULL,
                                  pat->user_data);
                }
                else
                {
                    if (_bAtCheckResp(pat, pat->recv_resp_data) == 0)
                    {
                        B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_OK, pat->recv_resp_data,
                                      pat->recv_resp_len, _bAtFree, pat->user_data);
                    }
                    else
                    {
                        B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_ERROR, pat->recv_resp_data,
                                      pat->recv_resp_len, _bAtFree, pat->user_data);
                    }
                    pat->recv_resp_data = NULL;
                    pat->recv_resp_len  = 0;
                }
#else
                PT_WAIT_UNTIL(pt, pat->resp == NULL, pat->timeout);
                if (PT_WAIT_IS_TIMEOUT(pt))
                {
                    _bAtReset(pat);
                    B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_TIMEOUT, NULL, 0, NULL,
                                  pat->user_data);
                }
#endif
            }
        }
#if _AT_DYNAMIC_ENABLE
        else
        {
            B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_UNKNOWN, pat->recv_resp_data,
                          pat->recv_resp_len, _bAtFree, pat->user_data);
        }
#endif
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup AT_Exported_Functions
 * \{
 */

int bAtInit(bAtStruct_t *pat, pAtCmdCb_t cmd_cb, pAtSendData_t send, void *user_data)
{
    if (pat == NULL || cmd_cb == NULL)
    {
        return -1;
    }
    b_log("at init..\r\n");
    memset(pat, 0, sizeof(bAtStruct_t));
    pat->cmd_cb    = cmd_cb;
    pat->send      = send;
    pat->pcmd      = NULL;
    pat->user_data = user_data;
    bTaskCreate("at", bAtTask, pat, &pat->attr);
    return 0;
}

int bAtSendCmd(bAtStruct_t *pat, const char *pcmd, const char *resp, uint16_t timeout)
{
    if (pat == NULL || pcmd == NULL || pat->cmd_cb == NULL || pat->send == NULL)
    {
        b_log_e("param error.%p,%p,%p,%p.\r\n", pat, pcmd, pat->cmd_cb, pat->send);
        return -1;
    }
    b_log("atcmd: %s\r\n", pcmd);
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
    pat->cmd_len = strlen(pcmd);
    pat->resp    = resp;
    pat->timeout = timeout;
    return 0;
}

int bAtSendCmd2(bAtStruct_t *pat, const char *pcmd, uint16_t cmd_len, const char *resp,
                uint16_t timeout)
{
    if (pat == NULL || pcmd == NULL || pat->cmd_cb == NULL || pat->send == NULL)
    {
        b_log_e("param error.%p,%p,%p,%p.\r\n", pat, pcmd, pat->cmd_cb, pat->send);
        return -1;
    }
    b_log("atcmd: %s\r\n", pcmd);
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
    pat->cmd_len = cmd_len;
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
#if _AT_DYNAMIC_ENABLE
    if (pat->recv_resp_data)
    {
        return -2;
    }

    if (pat->resp && (_bAtCheckResp(pat, pbuf) < 0))
    {
        return -1;
    }

    uint8_t *p = (uint8_t *)bMalloc(len + 1);
    if (p == NULL)
    {
        return -1;
    }
    memset(p, 0, len + 1);
    memcpy(p, pbuf, len);
    pat->recv_resp_data = (char *)p;
    pat->recv_resp_len  = len;
#else
    b_log("feed data.%s.\r\n", pbuf);
    if (pat->resp && (_bAtCheckResp(pat, (const char *)pbuf) == 0))
    {
        b_log("resp ok..\r\n");
        _bAtReset(pat);
        B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_OK, (char *)pbuf, len, NULL, pat->user_data);
    }
    else
    {
        B_SAFE_INVOKE(pat->cmd_cb, AT_CMD_RESULT_UNKNOWN, (char *)pbuf, len, NULL, pat->user_data);
    }
#endif
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
