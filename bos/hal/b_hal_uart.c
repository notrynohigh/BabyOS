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

static void _bHalUartDetectIdle()
{
    struct list_head   *pos    = NULL;
    bHalUartIdleAttr_t *pattr  = NULL;
    int                 retval = -1;
    list_for_each(pos, &bHalUartListHead)
    {
        pattr = list_entry(pos, bHalUartIdleAttr_t, list);
        if (pattr->index > 0)
        {
            if (pattr->l_index == 0)
            {
                pattr->l_index = pattr->index;
                pattr->l_tick  = bHalGetSysTick();
            }
            else
            {
                if (pattr->index != pattr->l_index)
                {
                    pattr->l_index = pattr->index;
                    pattr->l_tick  = bHalGetSysTick();
                }
                else if (bHalGetSysTick() - pattr->l_tick > pattr->idle_ms)
                {
                    if (pattr->callback)
                    {
                        retval = pattr->callback(pattr->pbuf, pattr->index, pattr->user_data);
                        if (retval >= 0 || pattr->index == pattr->len)
                        {
                            memset(pattr->pbuf, 0, pattr->len);
                            pattr->index = 0;
                        }
                    }
                    pattr->l_tick = bHalGetSysTick();
                }
            }
        }
    }
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section bos_polling
#endif
BOS_REG_POLLING_FUNC(_bHalUartDetectIdle);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif
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

__WEAKDEF int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    return -1;
}
#endif
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

int bHalUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    return bMcuUartSend(uart, pbuf, len);
}

int bHalReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    return bMcuReceive(uart, pbuf, len);
}

int bHalUartReceiveIdle(bHalUartNumber_t uart, bHalUartIdleAttr_t *attr)
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
    return retval;
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
