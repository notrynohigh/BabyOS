/**
 *!
 * \file        b_drv_mcumac.c
 * \version     v0.0.1
 * \date        2023/03/25
 * \author      babyos
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 babyos
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
#include "drivers/inc/b_drv_mcumac.h"

#include "core/inc/b_task.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup MCUMAC
 * \{
 */

/**
 * \defgroup MCUMAC_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MCUMAC_Private_Defines
 * \{
 */
#define DRIVER_NAME MCUMAC
#define DRIVER_PRIVATE_TYPE bMCUMACPrivate_t
/**
 * \}
 */

/**
 * \defgroup MCUMAC_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MCUMAC_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bMCUMAC_HalIf_t, DRIVER_NAME);

static bMCUMACPrivate_t bMCUMACRunInfo[bDRIVER_HALIF_NUM(bMCUMAC_HalIf_t, DRIVER_NAME)];
static bTaskAttr_t      bMcuMacTaskAttr;
/**
 * \}
 */

/**
 * \defgroup MCUMAC_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MCUMAC_Private_Functions
 * \{
 */

static int _bMCUMACWrite(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bHalEthTransmit(pbuf, len);
    return len;
}

static int _bMCUMACRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    void    *p        = NULL;
    void   **real_buf = (void **)pbuf;
    uint32_t rlen     = 0;
    if (real_buf == NULL)
    {
        return 0;
    }
    bHalEthReceive(&p, &rlen);
    if (p != NULL)
    {
        *real_buf = p;
    }
    return rlen;
}

static int _bMCUMACCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bDRIVER_GET_PRIVATE(_priv, DRIVER_PRIVATE_TYPE, pdrv);
    switch (cmd)
    {
        case bCMD_GET_DRIVER_NETIF:
        {
            if (param == NULL)
            {
                return -1;
            }
            ((bDriverNetif_t *)param)->private = _priv;
        }
        break;
        case bCMD_GET_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            bMacAddress_t *paddr = (bMacAddress_t *)param;
            bHalEthGetMacAddr(&paddr->address[0], sizeof(paddr->address));
        }
        break;
        case bCMD_SET_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            bMacAddress_t *paddr = (bMacAddress_t *)param;
            bHalEthSetMacAddr(&paddr->address[0], sizeof(paddr->address));
        }
        break;
        case bCMD_GET_LINK_STATE:
        {
            if (param == NULL)
            {
                return -1;
            }
            *((uint8_t *)param) = bMCUMACRunInfo[pdrv->drv_no].link_state;
        }
        break;
        case bCMD_REG_BUF_LIST:
        {
            if (param == NULL)
            {
                return -1;
            }
            bHalEthInit((bHalBufList_t *)param);
        }
        break;
        case bCMD_REG_LINK_CALLBACK:
        {
            if (param == NULL)
            {
                return -1;
            }
            bLinkStateCb_t *pcallback = (bLinkStateCb_t *)param;
            memcpy(&bMCUMACRunInfo[pdrv->drv_no].link_cb, pcallback, sizeof(bLinkStateCb_t));
            bMCUMACRunInfo[pdrv->drv_no].link_state = 0;
        }
        break;
    }
    return 0;
}

PT_THREAD(_bMcuMacLinkTask)(struct pt *pt, void *arg)
{
    static uint8_t dev_count  = 0;
    uint8_t        i          = 0;
    uint8_t        link_state = 0;
    B_TASK_INIT_BEGIN();
    dev_count = sizeof(bMCUMACRunInfo) / sizeof(bMCUMACPrivate_t);
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        for (i = 0; i < dev_count; i++)
        {
            link_state = bHalEthIsLinked();  // MCU 内置MAC可控制器默认只有1个
            if (link_state != bMCUMACRunInfo[i].link_state)
            {
                bMCUMACRunInfo[i].link_state = link_state;
                if (bMCUMACRunInfo[i].link_cb.cb)
                {
                    bMCUMACRunInfo[i].link_cb.cb(link_state, bMCUMACRunInfo[i].link_cb.arg);
                }
                bHalEthLinkUpdate(link_state);
            }
        }
        bTaskDelayMs(pt, 500);
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup MCUMAC_Exported_Functions
 * \{
 */
int bMCUMAC_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bMCUMAC_Init);
    pdrv->read  = _bMCUMACRead;
    pdrv->write = _bMCUMACWrite;
    pdrv->ctl   = _bMCUMACCtl;
    memset(&bMCUMACRunInfo[pdrv->drv_no], 0, sizeof(bMCUMACPrivate_t));
    pdrv->_private._p = &bMCUMACRunInfo[pdrv->drv_no];
    bTaskCreate("link", _bMcuMacLinkTask, NULL, &bMcuMacTaskAttr);
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_MCUMAC, bMCUMAC_Init);
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

/************************ Copyright (c) 2023 babyos*****END OF FILE****/
