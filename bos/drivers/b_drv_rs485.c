/**
 *!
 * \file        b_drv_rs485.c
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_rs485.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup RS485
 * \{
 */

/**
 * \defgroup RS485_Private_TypesDefinitions
 * \{
 */

typedef struct
{
#if defined(RS485_CACHE_BUF_LEN) && (RS485_CACHE_BUF_LEN > 0)
    uint8_t cache_buf[RS485_CACHE_BUF_LEN];
#else
    bRS485Callback_t cb;
#endif
    uint8_t            buf[RS485_RX_BUF_LEN];
    bHalUartIdleAttr_t attr;
} bRS485Private_t;

/**
 * \}
 */

/**
 * \defgroup RS485_Private_Defines
 * \{
 */
#define DRIVER_NAME RS485
/**
 * \}
 */

/**
 * \defgroup RS485_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup RS485_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bRS485_HalIf_t, DRIVER_NAME);
static bRS485Private_t bRS485RunInfo[bDRIVER_HALIF_NUM(bRS485_HalIf_t, DRIVER_NAME)];
/**
 * \}
 */

/**
 * \defgroup RS485_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup RS485_Private_Functions
 * \{
 */

static int _bHalUartIdleCallback(uint8_t *pbuf, uint16_t len, void *user_data)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)user_data;
#if defined(RS485_CACHE_BUF_LEN) && (RS485_CACHE_BUF_LEN > 0)
    bFIFO_Write(&pdrv->r_cache, pbuf, len);
#else
    bRS485Private_t *p = (bRS485Private_t *)pdrv->_private._p;
    if (p->cb)
    {
        p->cb(pbuf, len);
    }
#endif
    return 0;
}

static int _bRS485Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    int rlen = 0;
    if (len == 0)
    {
        return -1;
    }

#if defined(RS485_CACHE_BUF_LEN) && (RS485_CACHE_BUF_LEN > 0)
    rlen = bFIFO_Read(&pdrv->r_cache, pbuf, len);
#endif

    return rlen;
}

static int _bRS485Write(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    if (len == 0)
    {
        return -1;
    }
    bDRIVER_GET_HALIF(_if, bRS485_HalIf_t, pdrv);
    bHalGpioWritePin(_if->port, _if->pin, 1);
    bHalUartSend(_if->uart, pbuf, len);
    bHalGpioWritePin(_if->port, _if->pin, 0);
    return len;
}

static int _bRS485Ctl(struct bDriverIf *pdrv, uint8_t cmd, void *param)
{
    bRS485Private_t *p = (bRS485Private_t *)pdrv->_private._p;
    switch (cmd)
    {
        case bCMD_485_REG_CALLBACK:
        {
            p->cb = (bRS485Callback_t)param;
        }
        break;
        case bCMD_485_IDLE_MS:
        {
            if (param)
            {
                p->attr.idle_ms = *((uint16_t *)param);
            }
        }
        break;
        default:
            break;
    }
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup RS485_Exported_Functions
 * \{
 */
int bRS485_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bRS485_Init);
    pdrv->read  = _bRS485Read;
    pdrv->write = _bRS485Write;
    pdrv->ctl   = _bRS485Ctl;

    pdrv->_private._p = &bRS485RunInfo[pdrv->drv_no];
    memset(pdrv->_private._p, 0, sizeof(bRS485Private_t));

#if defined(RS485_CACHE_BUF_LEN) && (RS485_CACHE_BUF_LEN > 0)
    bDRIVER_SET_READCACHE(pdrv, bRS485RunInfo[pdrv->drv_no].cache_buf, RS485_CACHE_BUF_LEN);
#else
    bRS485RunInfo[pdrv->drv_no].cb = NULL;
#endif

    bHAL_UART_INIT_ATTR(&bRS485RunInfo[pdrv->drv_no].attr, bRS485RunInfo[pdrv->drv_no].buf,
                        RS485_RX_BUF_LEN, RS485_RX_IDLE_MS, _bHalUartIdleCallback, pdrv);
    bHalUartReceiveIdle(bHalIf_RS485[pdrv->drv_no].uart, &bRS485RunInfo[pdrv->drv_no].attr);
    bHalGpioWritePin(bHalIf_RS485[pdrv->drv_no].port, bHalIf_RS485[pdrv->drv_no].pin, 0);
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_RS485, bRS485_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section 
#endif
/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
