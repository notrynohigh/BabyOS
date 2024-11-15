/**
 *!
 * \file        b_drv_tmp112.c
 * \version     v0.0.1
 * \date        2024/11/15
 * \author      haimeng(haimeng.chen@qq.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 hmchen
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
#include "drivers/inc/b_drv_tmp112.h"

#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup TMP112
 * \{
 */

/**
 * \defgroup TMP112_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TMP112_Private_Defines
 * \{
 */
#define DRIVER_NAME TMP112

#define TEMPERATURE_REGISTER 0x00
#define CONFIG_REGISTER      0x01
#define T_LOW_REGISTER       0x02
#define T_HIGH_REGISTER      0x03

/**
 * \}
 */

/**
 * \defgroup TMP112_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TMP112_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bTMP112_HalIf_t, DRIVER_NAME);

/**
 * \}
 */

/**
 * \defgroup TMP112_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TMP112_Private_Functions
 * \{
 */

static int _bTMP112ReadTemperarute(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
	uint8_t reg = 0;
	uint8_t data[2] = {0};
    uint8_t _temp_lo = 0;
    uint8_t _temp_hi = 0;
    int16_t digitalTemp = 0;
	bTempVal_t temp_val;
	
	bDRIVER_GET_HALIF(_if, bTMP112_HalIf_t, pdrv);
	reg = TEMPERATURE_REGISTER;
	bHalI2CMemRead(_if, reg, 1, data, 2);
	_temp_hi = data[0];
	_temp_lo = data[1];
    // Bit 0 of second byte will always be 0 in 12-bit readings and 1 in 13-bit
    if(_temp_lo & 0x01) // 13 bit mode
    {
        // Combine bytes to create a signed int
        digitalTemp = ((_temp_hi) << 5) | (_temp_lo >> 3);
        // Temperature data can be + or -, if it should be negative,
        // convert 13 bit to 16 bit and use the 2s compliment.
        if(digitalTemp > 0xFFF)
        {
            digitalTemp |= 0xE000;
        }
    }
    else // 12 bit mode
    {
        // Combine bytes to create a signed int 
        digitalTemp = ((_temp_hi) << 4) | (_temp_lo >> 4);
        // Temperature data can be + or -, if it should be negative,
        // convert 12 bit to 16 bit and use the 2s compliment.
        if(digitalTemp > 0x7FF)
        {
            digitalTemp |= 0xF000;
        }
    }
	temp_val.tempx100 = digitalTemp * 0.0625*100;
	
    memcpy(pbuf, &temp_val, sizeof(bTempVal_t));
    return sizeof(bTempVal_t);
}

/**
 * \}
 */

/**
 * \addtogroup TMP112_Exported_Functions
 * \{
 */
int bTMP112_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bTMP112_Init);
    pdrv->read       = _bTMP112ReadTemperarute;
    pdrv->_private.v = 0;
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_TMP112, bTMP112_Init);
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

/************************ Copyright (c) 2024 hmchen *****END OF FILE****/
