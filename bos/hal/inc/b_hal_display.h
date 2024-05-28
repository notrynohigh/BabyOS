/**
 *!
 * \file        b_hal_display.h
 * \version     v0.0.1
 * \date        2021/06/13
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2021 Bean
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SGPIOL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_HAL_DISPLAY_H__
#define __B_HAL_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_hal_gpio.h"
#include "b_hal_spi.h"

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup DISPLAY
 * \{
 */

/**
 * \defgroup DISPLAY_Exported_TypesDefinitions
 * \{
 */

typedef struct
{
    union
    {
        uint32_t rw_addr;
        struct
        {
            bHalGPIOInstance_t data;
            bHalGPIOInstance_t rs;
            bHalGPIOInstance_t rd;
            bHalGPIOInstance_t wr;
            bHalGPIOInstance_t cs;
        } _io;
        struct
        {
            bHalGPIOInstance_t rs;
            bHalSPIIf_t        _spi;
        } _spi;
    } _if;
    // If reset pin is not used, set it to {B_HAL_GPIO_INVALID, B_HAL_PIN_INVALID}
    bHalGPIOInstance_t reset;
    uint8_t            if_type;  // 0: _io  1: rw_addr  2: _spi
} bLCD_HalIf_t;

#define LCD_IF_TYPE_IO (0)
#define LCD_IF_TYPE_RWADDR (1)
#define LCD_IF_TYPE_SPI (2)

/**
 * \}
 */

/**
 * \defgroup DISPLAY_Exported_Functions
 * \{
 */

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

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

/************************ Copyright (c) 2021 Bean *****END OF FILE****/
