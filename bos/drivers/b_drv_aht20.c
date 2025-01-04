/**
 *!
 * \file        b_drv_aht20.c
 * \version     v0.0.1
 * \date        2023/03/25
 * \author      babyos
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 babyos
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
#include "drivers/inc/b_drv_aht20.h"

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup AHT20
 * \{
 */

/**
 * \defgroup AHT20_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AHT20_Private_Defines
 * \{
 */
#define DRIVER_NAME AHT20

#define MIN_READ_TIM (500)

/**
 * \}
 */

/**
 * \defgroup AHT20_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AHT20_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bAHT20_HalIf_t, DRIVER_NAME);

static bAHT20Private_t bAHT20RunInfo[bDRIVER_HALIF_NUM(bAHT20_HalIf_t, DRIVER_NAME)];

/**
 * \}
 */

/**
 * \defgroup AHT20_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup AHT20_Private_Functions
 * \{
 */

static uint8_t _bAHT20CRC(uint8_t *pdata, uint8_t len)
{
    uint8_t _crc = 0xff, i, j;
    for (i = 0; i < len; i++)
    {
        _crc = _crc ^ *pdata;
        for (j = 0; j < 8; j++)
        {
            if (_crc & 0x80)
                _crc = (_crc << 1) ^ 0x31;
            else
                _crc <<= 1;
        }
        pdata++;
    }
    return _crc;
}

static int _bAHT20Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bAHT20_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bAHT20Private_t, pdrv);

    if (pbuf == NULL || len < sizeof(bTempHumidityVal_t))
    {
        return -1;
    }
    if (TICK_DIFF_BIT32(_priv->tick, bHalGetSysTick()) > MS2TICKS(MIN_READ_TIM))
    {
        uint8_t tmp[8];
        int32_t s32x  = 0;
        double  tmp_f = 0.001;
        bHalI2CMemRead(_if, 0, 1, tmp, 7);
        if ((_bAHT20CRC(tmp, 6) == tmp[6]) && ((tmp[0] & 0x98) == 0x18))
        {
            s32x = tmp[1];
            s32x = s32x << 8;
            s32x += tmp[2];
            s32x = s32x << 8;
            s32x += tmp[3];
            s32x = s32x >> 4;

            tmp_f                 = s32x;
            tmp_f                 = tmp_f * 100 / 1048576;
            _priv->value.humidity = (uint8_t)tmp_f;

            s32x = tmp[3] & 0x0F;
            s32x = s32x << 8;
            s32x += tmp[4];
            s32x = s32x << 8;
            s32x += tmp[5];

            tmp_f                 = s32x;
            tmp_f                 = tmp_f * 200 / 1048576 - 50;
            _priv->value.tempx100 = (int16_t)(tmp_f * 100);
        }
        else
        {
            b_log_e("crc error..\r\n");
            b_log_hex(tmp, 7);
        }
        uint8_t cmd_table[] = {0x33, 0x00};
        bHalI2CMemWrite(pdrv->hal_if, 0xAC, 1, cmd_table, 2);
        _priv->tick = bHalGetSysTick();
    }
    bTempHumidityVal_t *pval = (bTempHumidityVal_t *)pbuf;
    pval->humidity           = _priv->value.humidity;
    pval->tempx100           = _priv->value.tempx100;
    return sizeof(bTempHumidityVal_t);
}

static int _bAHT20Open(bDriverInterface_t *pdrv)
{
    return 0;
}

static int _bAHT20Close(bDriverInterface_t *pdrv)
{
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup AHT20_Exported_Functions
 * \{
 */
int bAHT20_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bAHT20_Init);
    pdrv->read        = _bAHT20Read;  // read: bTempHumidityVal_t
    pdrv->write       = NULL;
    pdrv->ctl         = NULL;
    pdrv->open        = _bAHT20Open;
    pdrv->close       = _bAHT20Close;
    pdrv->_private._p = &bAHT20RunInfo[pdrv->drv_no];
    memset(&bAHT20RunInfo[pdrv->drv_no], 0, sizeof(bAHT20Private_t));
    bHalDelayMs(40);
    uint8_t status      = 0;
    uint8_t cmd_table[] = {0x08, 0x00};
    uint8_t count       = 0;
    do
    {
        bHalI2CReadByte(pdrv->hal_if, &status, 1);
        if ((status & (0x1 << 3)) == 0)
        {
            bHalI2CMemWrite(pdrv->hal_if, 0xBE, 1, cmd_table, 2);
            bHalDelayMs(10);
        }
        count++;
        if (count > 3)
        {
            return -1;
        }
    } while ((status & (0x1 << 3)) == 0);

    cmd_table[0] = 0x33;
    bHalI2CMemWrite(pdrv->hal_if, 0xAC, 1, cmd_table, 2);
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_AHT20, bAHT20_Init);
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

/************************ Copyright (c) 2023 babyos*****END OF FILE****/
