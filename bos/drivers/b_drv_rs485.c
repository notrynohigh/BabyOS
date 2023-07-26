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
    uint8_t buf[RS485_RX_BUF_LEN];
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

static void _bHalItHandler(bHalItNumber_t it, uint8_t index, bHalItParam_t *param, void *user_data)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)user_data;
    bFIFO_Write(&pdrv->r_cache, param->_uart.pbuf, param->_uart.len);
}

static int _bRS485Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    int rlen = 0;
    if (len == 0)
    {
        return -1;
    }
    rlen = bFIFO_Read(&pdrv->r_cache, pbuf, len);
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

    pdrv->_private._p = &bRS485RunInfo[pdrv->drv_no];
    memset(pdrv->_private._p, 0, sizeof(bRS485Private_t));

    bDRIVER_SET_READCACHE(pdrv, &bRS485RunInfo[pdrv->drv_no].buf[0],
                          sizeof(bRS485RunInfo[pdrv->drv_no].buf));

    bHAL_IT_REGISTER(rs485_it, B_HAL_IT_UART_RX, bHalIf_RS485[pdrv->drv_no].uart, _bHalItHandler,
                     pdrv);

    bHalGpioWritePin(bHalIf_RS485[pdrv->drv_no].port, bHalIf_RS485[pdrv->drv_no].pin, 0);
    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_RS485, bRS485_Init);

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
