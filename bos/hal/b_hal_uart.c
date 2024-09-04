/**
 *!
 * \file        b_hal_uart.c
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
#include "b_section.h"
#include "core/inc/b_task.h"
#include "hal/inc/b_hal.h"

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup UART
 * \{
 */

/**
 * \defgroup UART_Private_Variables
 * \{
 */

static LIST_HEAD(bHalUartListHead);
B_TASK_CREATE_ATTR(bHalUartTaskAttr);
static bTaskId_t bHalUartTaskId = NULL;
/**
 * \}
 */

/**
 * \addtogroup UART_Private_Functions
 * \{
 */

static bHalUartIdleAttr_t *_bHalUartAttrFind(bHalUartNumber_t uart)
{
    struct list_head   *pos   = NULL;
    bHalUartIdleAttr_t *pattr = NULL;
    list_for_each(pos, &bHalUartListHead)
    {
        pattr = list_entry(pos, bHalUartIdleAttr_t, list);
        if (pattr->uart == uart)
        {
            return pattr;
        }
    }
    return NULL;
}

static void _bHalItHandler(bHalItNumber_t it, uint8_t index, bHalItParam_t *param, void *user_data)
{
    bHalUartIdleAttr_t *pattr = (bHalUartIdleAttr_t *)user_data;
    int                 i     = 0;
    if (pattr == NULL || pattr->pbuf == NULL)
    {
        return;
    }
    for (i = 0; i < param->_uart.len; i++)
    {
        if (pattr->index < pattr->len)
        {
            pattr->pbuf[pattr->index] = param->_uart.pbuf[i];
            pattr->index += 1;
        }
    }
}

PT_THREAD(_bHalUartDetectIdle)(struct pt *pt, void *arg)
{
    struct list_head   *pos    = NULL;
    bHalUartIdleAttr_t *pattr  = NULL;
    int                 retval = -1;
    B_TASK_INIT_BEGIN();
    // ...
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        if (list_empty(&bHalUartListHead))
        {
            bTaskRestart(pt);
        }
        list_for_each(pos, &bHalUartListHead)
        {
            pattr = list_entry(pos, bHalUartIdleAttr_t, list);
            if (pattr->chl < B_HAL_DMA_CHL_NUMBER)
            {
                pattr->index = pattr->len - bHalDmaGetCount(pattr->chl);
            }
            if (pattr->index > 0)
            {
                if (pattr->l_index == 0 ||
                    ((pattr->l_index > 0) && (pattr->index != pattr->l_index)))
                {
                    pattr->l_index = pattr->index;
                    pattr->l_tick  = bHalGetSysTick();
                }
                if (TICK_DIFF_BIT32(pattr->l_tick, bHalGetSysTick()) > MS2TICKS(pattr->idle_ms))
                {
                    if (pattr->callback)
                    {
                        retval = pattr->callback(pattr->pbuf, pattr->index, pattr->user_data);
                        if (retval >= 0 || pattr->index == pattr->len)
                        {
                            memset(pattr->pbuf, 0, pattr->len);
                            pattr->index = 0;
                            if (pattr->chl < B_HAL_DMA_CHL_NUMBER)
                            {
                                bHalDmaStop(pattr->chl);
                                bHalDmaSetDest(pattr->chl, (uint32_t)pattr->pbuf);
                                bHalDmaSetCount(pattr->chl, pattr->len);
                                bHalDmaStart(pattr->chl);
                            }
                        }
                    }
                    pattr->l_tick = bHalGetSysTick();
                }
            }
        }
        bTaskDelayMs(pt, 5);
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup UART_Exported_Functions
 * \{
 */
#if defined(__WEAKDEF)
__WEAKDEF int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    return -1;
}

__WEAKDEF int bMcuUartReceiveDma(bHalUartNumber_t uart, bHalDmaConfig_t *pconf)
{
    return -1;
}

#endif
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

int bHalUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    if (IS_NULL(pbuf) || len == 0)
    {
        return -1;
    }
    return bMcuUartSend(uart, pbuf, len);
}

int bHalUartReceive(bHalUartNumber_t uart, bHalUartIdleAttr_t *attr)
{
    int                 retval = -1;
    bHalUartIdleAttr_t *pattr  = _bHalUartAttrFind(uart);
    if (pattr != NULL || attr == NULL || attr->pbuf == NULL)
    {
        return -1;
    }
    attr->uart         = uart;
    attr->it.it        = B_HAL_IT_UART_RX;
    attr->it.index     = uart;
    attr->it.handler   = _bHalItHandler;
    attr->it.user_data = attr;
    attr->index        = 0;
    attr->l_index      = 0;
    attr->l_tick       = 0;
    if (0 == (retval = bHalItRegister(&attr->it)))
    {
        list_add(&attr->list, &bHalUartListHead);
    }
    if (bHalUartTaskId == NULL)
    {
        bHalUartTaskId = bTaskCreate("uart", _bHalUartDetectIdle, NULL, &bHalUartTaskAttr);
    }
    return retval;
}

int bHalUartReceiveDma(bHalUartNumber_t uart, bHalUartIdleAttr_t *attr, bHalDmaChlNumber_t chl)
{
    bHalUartIdleAttr_t *pattr  = _bHalUartAttrFind(uart);
    if (pattr != NULL || attr == NULL || attr->pbuf == NULL || chl >= B_HAL_DMA_CHL_NUMBER)
    {
        return -1;
    }
    attr->uart    = uart;
    attr->chl     = chl;
    attr->index   = 0;
    attr->l_index = 0;
    attr->l_tick  = 0;

    bHalDmaConfig_t dma_conf;
    dma_conf.dest        = (uint32_t)attr->pbuf;
    dma_conf.src         = 0;
    dma_conf.bits        = B_DMA_DATA_BIT8;
    dma_conf.chl         = attr->chl;
    dma_conf.count       = attr->len;
    dma_conf.inc         = B_DMA_DEST_ADDR_INC;
    dma_conf.is_circular = 0;
    if (bMcuUartReceiveDma(uart, &dma_conf) < 0)
    {
        return -1;
    }
    list_add(&attr->list, &bHalUartListHead);
    if (bHalUartTaskId == NULL)
    {
        bHalUartTaskId = bTaskCreate("uart", _bHalUartDetectIdle, NULL, &bHalUartTaskAttr);
    }
    bHalDmaStart(chl);
    return 0;
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
