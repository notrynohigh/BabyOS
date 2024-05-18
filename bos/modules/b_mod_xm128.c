/**
 *!
 * \file        b_mod_xm128.c
 * \version     v0.0.1
 * \date        2020/02/03
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

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_xm128.h"

#if (defined(_XMODEM128_ENABLE) && (_XMODEM128_ENABLE == 1))

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup XMODEM128
 * \{
 */

/**
 * \defgroup XMODEM128_Private_TypesDefinitions
 * \{
 */
typedef struct
{
    uint8_t soh;
    uint8_t number;
    uint8_t xnumber;
    uint8_t dat[128];
    uint8_t check;
} bXmodem128Struct_t;

/**
 * \}
 */

/**
 * \defgroup XMODEM128_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup XMODEM128_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup XMODEM128_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup XMODEM128_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup XMODEM128_Private_Functions
 * \{
 */
static uint8_t _bXmodem128CalCheck(uint8_t *pbuf, uint8_t len)
{
    uint8_t tmp = 0;
    while (len--)
    {
        tmp += *pbuf++;
    }
    return tmp;
}

static int _bXmodem128Parse(void *attr, uint8_t *in, uint16_t i_len, uint8_t *out, uint16_t o_len)
{
    int                 ret   = -1;
    bProtocolAttr_t    *pattr = (bProtocolAttr_t *)attr;
    bXmodem128Struct_t *phead = (bXmodem128Struct_t *)in;
    bXYModemCbParam_t   param;
    if (phead->soh == XMODEM128_SOH && (phead->number | phead->xnumber) == 0xff &&
        i_len >= sizeof(bXmodem128Struct_t) &&
        _bXmodem128CalCheck(in, sizeof(bXmodem128Struct_t) - 1) == phead->check)
    {
        ret = 0;
    }
    else if (phead->soh == XMODEM128_EOT)
    {
        ret = 1;
    }

    if (ret != -1)
    {
        param.seq     = (ret == 0) ? phead->number : 0;
        param.dat     = (ret == 0) ? phead->dat : NULL;
        param.dat_len = (ret == 0) ? 128 : 0;
        B_SAFE_INVOKE(pattr->callback, B_XYMODEM_DATA, &param, pattr->arg);
    }

    if (out && o_len > 0)
    {
        if (ret == 0)
        {
            out[0] = XMODEM128_ACK;
            ret    = 1;
        }
        else if (ret == -1)
        {
            out[0] = XMODEM128_NAK;
            ret    = 1;
        }
        else
        {
            ret = 0;
        }
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static int _bXmodem128Package(void *attr, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len)
{
    int ret = -1;
    if (buf == NULL || buf_len == 0)
    {
        return -1;
    }

    if (cmd == B_XYMODEM_CMD_START)
    {
        buf[0] = XMODEM128_NAK;
        ret    = 1;
    }
    if (cmd == B_XYMODEM_CMD_STOP)
    {
        buf[0] = XMODEM128_CAN;
        ret    = 1;
    }
    return ret;
}

/**
 * \}
 */

/**
 * \addtogroup XMODEM128_Exported_Functions
 * \{
 */
#ifdef BSECTION_NEED_PRAGMA
#pragma section b_srv_protocol
#endif
bPROTOCOL_REG_INSTANCE("xmodem128", _bXmodem128Parse, _bXmodem128Package);
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

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
