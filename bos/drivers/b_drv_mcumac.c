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
    void    *p    = NULL;
    uint32_t rlen = 0;
    bHalEthReceive(&p, &rlen);
    if (p != NULL)
    {
        memcpy(pbuf, (void *)&p, sizeof(void *));
    }
    return rlen;
}

static int _bMCUMACCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    switch (cmd)
    {
        case bCMD_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            bMacAddress_t *paddr = (bMacAddress_t *)param;
            bHalEthGetMacAddr(&paddr->address[0], sizeof(paddr->address));
        }
        break;
        case bCMD_GET_LINK_STATE:
        {
            if (param == NULL)
            {
                return -1;
            }
            uint8_t link_state  = 0;
            link_state          = bHalEthIsLinked();
            *((uint8_t *)param) = link_state;
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
        case bCMD_LINK_STATE_CHANGE:
        {
            if (param == NULL)
            {
                return -1;
            }
            bHalEthLinkUpdate(*((uint8_t *)param));
        }
        break;
    }
    return 0;
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
    pdrv->read        = _bMCUMACRead;
    pdrv->write       = _bMCUMACWrite;
    pdrv->ctl         = _bMCUMACCtl;
    pdrv->_private._p = &bMCUMACRunInfo[pdrv->drv_no];
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
