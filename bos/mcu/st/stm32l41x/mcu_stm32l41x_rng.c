/**
 *!
 * \file        mcu_stm32l4xx_rng.c
 * \version     v0.0.1
 * \date        2020/03/25
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SUARTL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_rng.h"

#if (defined(STM32L41X_L46X))

typedef struct
{
    volatile uint32_t CR; /*!< RNG control register, Address offset: 0x00 */
    volatile uint32_t SR; /*!< RNG status register,  Address offset: 0x04 */
    volatile uint32_t DR; /*!< RNG data register,    Address offset: 0x08 */
} McuRNGReg_t;

#define RNG_BASE_ADDR (0x50060800UL)
#define MCU_RNG ((McuRNGReg_t *)RNG_BASE_ADDR)

uint32_t bMcuRNGRead()
{
    McuRNGReg_t *rng = MCU_RNG;
    if ((rng->CR & (0x1 << 2)) == 0)
    {
        rng->CR |= (0x1 << 2);
    }
    while ((rng->SR & 0x1) == 0)
    {
        (void)0;
    }
    return rng->DR;
}

#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
