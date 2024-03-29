/**
 *!
 * \file        b_hal_eth.c
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
#include "hal/inc/b_hal.h"

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup ETH
 * \{
 */

/**
 * \addtogroup ETH_Exported_Functions
 * \{
 */
#if defined(__WEAKDEF)
__WEAKDEF int bMcuEthInit(bHalBufList_t *pbuf_list)
{
    return -1;
}
__WEAKDEF int bMcuEthGetMacAddr(uint8_t *paddr, uint8_t len)
{
    return -1;
}

__WEAKDEF uint8_t bMcuEthIsLinked()
{
    return 0;
}

__WEAKDEF int bMcuEthLinkUpdate(uint8_t link_state)
{
    return 0;
}

__WEAKDEF int bMcuEthReceive(void **pbuf, uint32_t *plen)
{
    return -1;
}

__WEAKDEF int bMcuEthTransmit(void *pbuf, uint32_t len)
{
    return -1;
}
#endif
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

int bHalEthInit(bHalBufList_t *pbuf_list)
{
    if (pbuf_list == NULL)
    {
        return -1;
    }
    return bMcuEthInit(pbuf_list);
}

int bHalEthGetMacAddr(uint8_t *paddr, uint8_t len)
{
    if (paddr == NULL || len == 0)
    {
        return -1;
    }
    return bMcuEthGetMacAddr(paddr, len);
}

uint8_t bHalEthIsLinked()
{
    return bMcuEthIsLinked();
}

int bHalEthLinkUpdate(uint8_t link_state)
{
    return bMcuEthLinkUpdate(link_state);
}

int bHalEthReceive(void **pbuf, uint32_t *plen)
{
    if (pbuf == NULL || plen == 0)
    {
        return -1;
    }
    return bMcuEthReceive(pbuf, plen);
}

int bHalEthTransmit(void *pbuf, uint32_t len)
{
    if (pbuf == NULL || len == 0)
    {
        return -1;
    }
    return bMcuEthTransmit(pbuf, len);
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
