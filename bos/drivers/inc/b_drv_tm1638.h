/**
 *!
 * \file        b_drv_tm1638.h
 * \version     v0.0.1
 * \date        2024/08/14
 * \author      hmchen(chenhaimeng@189.cn)
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
#ifndef __B_DRV_TM1638_H__
#define __B_DRV_TM1638_H__

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
 * \addtogroup TM1638
 * \{
 */

/**
 * \defgroup TM1638_Exported_TypesDefinitions
 * \{
 */
//<HALIF 

typedef struct
{
	bHalGPIOInstance_t stb;
	bHalGPIOInstance_t clk;
	bHalGPIOInstance_t dio;
} bADS124X_HalIf_t;


#define TM1638_CONFIG_SUPPORT_COM_ANODE  1

#ifndef TM1638_CONFIG_SUPPORT_COM_ANODE
  #define TM1638_CONFIG_SUPPORT_COM_ANODE  1
#endif

/* Exported Constants -----------------------------------------------------------*/
#define TM1638DisplayTypeComCathode 0
#define TM1638DisplayTypeComAnode   1

#define TM1638DisplayStateOFF 0
#define TM1638DisplayStateON  1

#define TM1638DecimalPoint    0x80

/**
 * @brief  Data type of library functions result
 */
typedef enum TM1638_Result_e
{
  TM1638_OK      = 0,
  TM1638_FAIL    = -1,
} TM1638_Result_t;



typedef struct
{
  uint8_t DisplayType;

#if (TM1638_CONFIG_SUPPORT_COM_ANODE)
  uint8_t DisplayRegister[16];
#endif
	
} bTm1638Private_t;
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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
