/**
 *!
 * \file        b_mod_ymodem.c
 * \version     v0.0.1
 * \date        2020/02/12
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
#include "modules/inc/b_mod_ymodem.h"
#if (defined(_YMODEM_ENABLE) && (_YMODEM_ENABLE == 1))

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup YMODEM
 * \{
 */

/**
 * \defgroup YMODEM_Private_TypesDefinitions
 * \{
 */
typedef struct
{
    uint8_t soh;
    uint8_t number;
    uint8_t xnumber;
    uint8_t dat[128];
    uint8_t crc_h;
    uint8_t crc_l;
} bYmodem128Struct_t;

typedef struct
{
    uint8_t stx;
    uint8_t number;
    uint8_t xnumber;
    uint8_t dat[1024];
    uint8_t crc_h;
    uint8_t crc_l;
} bYmodem1kStruct_t;

/**
 * \}
 */

/**
 * \defgroup YMODEM_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup YMODEM_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup YMODEM_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup YMODEM_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup YMODEM_Private_Functions
 * \{
 */
static uint16_t _bYmodemCalCheck(uint8_t *pbuf, uint16_t len)
{
    uint16_t crc = 0;
    uint16_t i, j;

    for (i = 0; i < len; i++)
    {
        crc = crc ^ pbuf[i] << 8;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
            {
                crc = crc << 1 ^ 0x1021;
            }
            else
            {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

static int _bYmodemParse(void *attr, uint8_t *in, uint16_t i_len, uint8_t *out, uint16_t o_len)
{
    int                 ret   = -1;
    bProtocolAttr_t    *pattr = (bProtocolAttr_t *)attr;
    bYmodem128Struct_t *phead = (bYmodem128Struct_t *)in;
    bXYModemCbParam_t   param;
    uint16_t            crc = 0;
    if (phead->soh == YMODEM_SOH && (phead->number | phead->xnumber) == 0xff &&
        i_len >= sizeof(bYmodem128Struct_t))
    {
        crc = _bYmodemCalCheck(in + 3, sizeof(bYmodem128Struct_t) - 5);
        if (((crc & 0xff00) >> 8) == in[sizeof(bYmodem128Struct_t) - 2] ||
            (crc & 0xff) == in[sizeof(bYmodem128Struct_t) - 1])
        {
            param.seq     = phead->number;
            param.dat     = phead->dat;
            param.dat_len = 128;
            ret           = 0;
        }
    }
    else if (phead->soh == YMODEM_STX && (phead->number | phead->xnumber) == 0xff &&
             i_len >= sizeof(bYmodem1kStruct_t))
    {
        crc = _bYmodemCalCheck(in + 3, sizeof(bYmodem1kStruct_t) - 5);
        if (((crc & 0xff00) >> 8) == in[sizeof(bYmodem1kStruct_t) - 2] ||
            (crc & 0xff) == in[sizeof(bYmodem1kStruct_t) - 1])
        {
            param.seq     = phead->number;
            param.dat     = phead->dat;
            param.dat_len = 1024;
            ret           = 0;
        }
    }
    else if (phead->soh == YMODEM_EOT)
    {
        param.seq     = 0;
        param.dat     = NULL;
        param.dat_len = 0;
        ret           = 0;
    }

    if (ret != -1)
    {
        B_SAFE_INVOKE(pattr->callback, B_XYMODEM_DATA, &param, pattr->arg);
    }

    if (out && o_len > 0)
    {

        if (ret == -1)
        {
            out[0] = YMODEM_NAK;
            ret    = 1;
        }
        else if (param.dat != NULL)
        {
            if (param.seq == 0)
            {
                out[0]          = YMODEM_ACK;
                out[1]          = YMODEM_C;
                pattr->reserved = 0;
                ret             = 2;
            }
            else
            {
                out[0] = YMODEM_ACK;
                ret    = 1;
            }
        }
        else
        {
            if (pattr->reserved == 0)
            {
                out[0]          = YMODEM_NAK;
                pattr->reserved = 1;
                ret             = 1;
            }
            else
            {
                out[0]          = YMODEM_ACK;
                out[1]          = YMODEM_C;
                pattr->reserved = 0;
                ret             = 2;
            }
        }
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static int _bYmodemPackage(void *attr, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len)
{
    int ret = -1;
    if (buf == NULL || buf_len == 0)
    {
        return -1;
    }

    if (cmd == B_XYMODEM_CMD_START)
    {
        buf[0] = YMODEM_C;
        ret    = 1;
    }
    if (cmd == B_XYMODEM_CMD_STOP)
    {
        buf[0] = YMODEM_CAN;
        ret    = 1;
    }
    return ret;
}

/**
 * \}
 */

/**
 * \addtogroup YMODEM_Exported_Functions
 * \{
 */
#ifdef BSECTION_NEED_PRAGMA
#pragma section b_srv_protocol
#endif
bPROTOCOL_REG_INSTANCE("ymodem", _bYmodemParse, _bYmodemPackage);
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
