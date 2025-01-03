/**
 *!
 * \file        b_drv_bq769x2.h
 * \version     v0.0.1
 * \date        2024/12/30
 * \author      hmchen(chenhaimeng@189.cn)
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
#ifndef __B_DRV_BQ769X2H__
#define __B_DRV_BQ769X2H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_driver.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup BQ769X2
 * \{
 */

/**
 * \defgroup BQ769X2_Exported_TypesDefinitions
 * \{
 */
//<HALIF 1 I2C
typedef bHalI2CIf_t bBQ769X2_HalIf_t;

typedef struct
{
    bBMS_AFE_BQ769X2_Value_t value;
} bBQ769X2Private_t;

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2024 hmchen *****END OF FILE****/
