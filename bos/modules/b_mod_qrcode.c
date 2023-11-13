/**
 *!
 * \file        b_mod_qrcode.c
 * \version     v0.0.1
 * \date        2020/05/16
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
#include "modules/inc/b_mod_qrcode.h"

#if (defined(_QRCODE_ENABLE) && (_QRCODE_ENABLE == 1))
/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup QRCODE
 * \{
 */

/**
 * \defgroup QRCODE_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QRCODE_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QRCODE_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QRCODE_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QRCODE_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QRCODE_Private_Functions
 * \{
 */
static uint8_t _bQRCodeDataIsText(uint8_t *pdata, uint16_t data_len)
{
    uint16_t i   = 0;
    char     tmp = 0;
    for (i = 0; i < data_len; i++)
    {
        tmp = pdata[i];
        if ((tmp >= '0' && tmp <= '9') || (tmp >= 'A' && tmp <= 'Z') || tmp == ' ' || tmp == '$' ||
            tmp == '%' || tmp == '*' || tmp == '+' || tmp == '-' || tmp == '.' || tmp == '/' ||
            tmp == ':')
        {
            continue;
        }
        return 0;
    }
    return 1;
}
/**
 * \}
 */

/**
 * \addtogroup QRCODE_Exported_Functions
 * \{
 */

int bQRCodeCreate(bQRCode_t *pInstance, uint8_t *pdata, uint16_t data_len)
{
    int retval = 0;
    if (pInstance == NULL || pdata == NULL || data_len == 0)
    {
        return -1;
    }
    if (_bQRCodeDataIsText(pdata, data_len))
    {
        retval = qrcode_initText(&(pInstance->qr), pInstance->buf, pInstance->size, pInstance->ecc,
                                 (const char *)pdata);
    }
    else
    {
        retval = qrcode_initBytes(&(pInstance->qr), pInstance->buf, pInstance->size, pInstance->ecc,
                                  pdata, data_len);
    }
    return retval;
}

int bQRCodeGetValue(bQRCode_t *pInstance, uint8_t x, uint8_t y)
{
    int retval = 0;
    retval     = qrcode_getModule(&(pInstance->qr), x, y);
    return retval;
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
