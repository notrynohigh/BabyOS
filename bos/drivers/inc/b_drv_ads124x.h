/**
 *!
 * \file        b_drv_ads124x.h
 * \version     v0.0.1
 * \date        2024/08/14
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
#ifndef __B_DRV_ADS124X_H__
#define __B_DRV_ADS124X_H__

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
 * \addtogroup ADS124X
 * \{
 */

/**
 * \defgroup ADS124X_Exported_TypesDefinitions
 * \{
 */
//<HALIF

typedef struct
{
    bHalSPIIf_t        _spi;
    bHalGPIOInstance_t drdy;
    bHalGPIOInstance_t start;
    bHalGPIOInstance_t reset;
    bHalItIf_t         it;
} bADS124X_HalIf_t;

/* Error Return Values */
#define ADS1248_NO_ERROR 0
#define ADS1248_ERROR -1

typedef struct
{
    uint8_t IDACroute;
    uint8_t IDACdir;
} bADS124X_IDACRouting_t;

typedef struct
{
    uint8_t AINN;
    uint8_t AINP;
} bADS124X_InputPin_t;

typedef struct
{
    uint32_t              data[32];
    bAds124xDrvCallback_t cb;
    bHalIt_t              it_instance;
} bAds124xPrivate_t;

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
