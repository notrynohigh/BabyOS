/**
 *!
 * \file        b_hal_dma.c
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
 * LIABILITY, WHDMAER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
/*Includes ----------------------------------------------*/
#include "hal/inc/b_hal.h"

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup DMA
 * \{
 */

/**
 * \addtogroup DMA_Exported_Functions
 * \{
 */
#if defined(__WEAKDEF)

__WEAKDEF int bMcuDmaConfig(bHalDmaConfig_t *pconf)
{
    return -1;
}

__WEAKDEF int bMcuDmaStart(bHalDmaChlNumber_t chl)
{
    return -1;
}

__WEAKDEF int bMcuDmaStop(bHalDmaChlNumber_t chl)
{
    return -1;
}

__WEAKDEF int bMcuDmaSetDest(bHalDmaChlNumber_t chl, uint32_t addr)
{
    return -1;
}
__WEAKDEF int bMcuDmaSetCount(bHalDmaChlNumber_t chl, uint32_t count)
{
    return -1;
}
__WEAKDEF int bMcuDmaGetCount(bHalDmaChlNumber_t chl)
{
    return -1;
}

#endif
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

int bHalDmaConfig(bHalDmaConfig_t *pconf)
{
    if (pconf == NULL || pconf->chl >= B_HAL_DMA_CHL_NUMBER)
    {
        return -1;
    }
    return bMcuDmaConfig(pconf);
}

int bHalDmaStart(bHalDmaChlNumber_t chl)
{
    if (chl >= B_HAL_DMA_CHL_NUMBER)
    {
        return -1;
    }
    return bMcuDmaStart(chl);
}

int bHalDmaStop(bHalDmaChlNumber_t chl)
{
    if (chl >= B_HAL_DMA_CHL_NUMBER)
    {
        return -1;
    }
    return bMcuDmaStop(chl);
}

int bHalDmaSetDest(bHalDmaChlNumber_t chl, uint32_t addr)
{
    if (chl >= B_HAL_DMA_CHL_NUMBER)
    {
        return -1;
    }
    return bMcuDmaSetDest(chl, addr);
}

int bHalDmaSetCount(bHalDmaChlNumber_t chl, uint32_t count)
{
    if (chl >= B_HAL_DMA_CHL_NUMBER)
    {
        return -1;
    }
    return bMcuDmaSetCount(chl, count);
}
int bHalDmaGetCount(bHalDmaChlNumber_t chl)
{
    if (chl >= B_HAL_DMA_CHL_NUMBER)
    {
        return -1;
    }
    return bMcuDmaGetCount(chl);
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

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
