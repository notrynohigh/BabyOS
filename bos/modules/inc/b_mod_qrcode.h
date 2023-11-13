/**
 *!
 * \file        b_mod_qrcode.h
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
#ifndef __B_MOD_QRCODE_H__
#define __B_MOD_QRCODE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_QRCODE_ENABLE) && (_QRCODE_ENABLE == 1))

#include "thirdparty/qrcode/qrcode.h"

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
 * \defgroup QRCODE_Exported_TypesDefinitions
 * \{
 */
typedef enum
{
    QRCODE_21X21 = 1,
    QRCODE_25X25,
    QRCODE_29X29,
    QRCODE_33X33,
    QRCODE_37X37,
    QRCODE_41X41,
    QRCODE_45X45,
    QRCODE_49X49,
    QRCODE_53X53,
    QRCODE_57X57,
    QRCODE_61X61,
    QRCODE_65X65,
    QRCODE_69X69,
    QRCODE_73X73,
    QRCODE_77X77,
    QRCODE_81X81,
    QRCODE_85X85,
    QRCODE_89X89,
    QRCODE_93X93,
    QRCODE_97X97,
    QRCODE_101X101,
    QRCODE_105X105,
    QRCODE_109X109,
    QRCODE_113X113,
    QRCODE_117X117,
    QRCODE_121X121,
    QRCODE_125X125,
    QRCODE_129X129,
    QRCODE_133X133,
    QRCODE_137X137,
    QRCODE_141X141,
    QRCODE_145X145,
    QRCODE_149X149,
    QRCODE_153X153,
    QRCODE_157X157,
    QRCODE_161X161,
    QRCODE_165X165,
    QRCODE_169X169,
    QRCODE_173X173,
    QRCODE_177X177,
} bQRCodeSize_t;

typedef enum
{
    QRCODE_ECC_LOW = 0,
    QRCODE_ECC_MEDIUM,
    QRCODE_ECC_QUARTILE,
    QRCODE_ECC_HIGH,
} bQRCodeEcc_t;

typedef struct
{
    QRCode        qr;
    bQRCodeSize_t size;
    bQRCodeEcc_t  ecc;
    uint8_t      *buf;
} bQRCode_t;

/**
 * \}
 */

/**
 * \defgroup QRCODE_Exported_Defines
 * \{
 */
// qr_size \ref bQRCodeSize_t
// qr_ecc \ref bQRCodeEcc_t
#define bQRCODE_INSTANCE(name, qr_size, qr_ecc)                               \
    uint8_t   modules_buf[((qr_size * 4 + 17) * (qr_size * 4 + 17) + 7) / 8]; \
    bQRCode_t name = {                                                        \
        .size = qr_size,                                                      \
        .buf  = &modules_buf[0],                                              \
        .ecc  = qr_ecc,                                                       \
    }


/**
 * \}
 */

/**
 * \defgroup QRCODE_Exported_Functions
 * \{
 */

/**
 * \brief 创建二维码数据
 * \param pInstance  实例，通过bRQCODE_INSTANCE创建
 * \param pdata      文本数据
 * \param data_len   数据长度
 * \return int
 */
int bQRCodeCreate(bQRCode_t *pInstance, uint8_t *pdata, uint16_t data_len);
/**
 * \brief 获取坐标处数据
 * \param pInstance 实例
 * \param x
 * \param y
 * \return int  0 or 1
 */
int bQRCodeGetValue(bQRCode_t *pInstance, uint8_t x, uint8_t y);
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
