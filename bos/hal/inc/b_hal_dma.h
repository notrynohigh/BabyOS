/**
 *!
 * \file        b_hal_dma.h
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
 * LIABILITY, WHDMAER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_HAL_DMA_H__
#define __B_HAL_DMA_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup DMA
 * \{
 */

/**
 * \defgroup DMA_Exported_TypesDefinitions
 * \{
 */
typedef enum
{
    B_HAL_DMA_CHL_1,
    B_HAL_DMA_CHL_2,
    B_HAL_DMA_CHL_3,
    B_HAL_DMA_CHL_4,
    B_HAL_DMA_CHL_5,
    B_HAL_DMA_CHL_6,
    B_HAL_DMA_CHL_7,
    B_HAL_DMA_CHL_8,
    B_HAL_DMA_CHL_9,
    B_HAL_DMA_CHL_10,
    B_HAL_DMA_CHL_11,
    B_HAL_DMA_CHL_12,
    B_HAL_DMA_CHL_13,
    B_HAL_DMA_CHL_14,
    B_HAL_DMA_CHL_15,
    B_HAL_DMA_CHL_16,
    B_HAL_DMA_CHL_NUMBER,
    B_HAL_DMA_CHL_INVALID
} bHalDmaChlNumber_t;

typedef enum
{
    B_DMA_SRC_ADDR_INC,
    B_DMA_DEST_ADDR_INC,
    B_DMA_BOTH_INC,
    B_DMA_NONE_INC
} bHalDmaAddrInc_t;

typedef enum
{
    B_DMA_DATA_BIT8,
    B_DMA_DATA_BIT16,
    B_DMA_DATA_BIT32
} bHalDmaDataBits_t;

typedef enum
{
    B_DMA_REQ_UART1_RX,
    B_DMA_REQ_UART2_RX,
    B_DMA_REQ_UART3_RX,
    B_DMA_REQ_LPUART1_RX,
} bHalDmaRequest_t;

typedef struct
{
    bHalDmaChlNumber_t chl;
    uint32_t           src;
    uint32_t           dest;
    bHalDmaAddrInc_t   inc;
    bHalDmaDataBits_t  bits;
    uint32_t           count;
    bHalDmaRequest_t   request;
    uint8_t            is_circular;
} bHalDmaConfig_t;

/**
 * \}
 */

/**
 * \defgroup DMA_Exported_Functions
 * \{
 */
int bMcuDmaConfig(bHalDmaConfig_t *pconf);
int bMcuDmaStart(bHalDmaChlNumber_t chl);
int bMcuDmaStop(bHalDmaChlNumber_t chl);
int bMcuDmaSetDest(bHalDmaChlNumber_t chl, uint32_t addr);
int bMcuDmaSetCount(bHalDmaChlNumber_t chl, uint32_t count);
int bMcuDmaGetCount(bHalDmaChlNumber_t chl);
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
int bHalDmaConfig(bHalDmaConfig_t *pconf);
int bHalDmaStart(bHalDmaChlNumber_t chl);
int bHalDmaStop(bHalDmaChlNumber_t chl);
int bHalDmaSetDest(bHalDmaChlNumber_t chl, uint32_t addr);
int bHalDmaSetCount(bHalDmaChlNumber_t chl, uint32_t count);
int bHalDmaGetCount(bHalDmaChlNumber_t chl);
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
