/**
 *!
 * \file        mcu_stm32f10x_eth.c
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include <string.h>

#include "b_config.h"
#include "hal/inc/b_hal_eth.h"
#include "utils/inc/b_util_log.h"
#if (defined(STM32F10X_LD) || defined(STM32F10X_MD) || defined(STM32F10X_HD) || \
     defined(STM32F10X_CL))

#if (defined(USE_HAL_DRIVER))

#include "stm32f1xx_hal.h"

#if (defined(HAL_ETH_MODULE_ENABLED))

#if defined(__ICCARM__) /*!< IAR Compiler */
#pragma data_alignment = 4
#endif
__ALIGN_BEGIN ETH_DMADescTypeDef DMARxDscrTab[ETH_RXBUFNB] __ALIGN_END;

#if defined(__ICCARM__) /*!< IAR Compiler */
#pragma data_alignment = 4
#endif
__ALIGN_BEGIN ETH_DMADescTypeDef DMATxDscrTab[ETH_TXBUFNB] __ALIGN_END;

#if defined(__ICCARM__) /*!< IAR Compiler */
#pragma data_alignment = 4
#endif
__ALIGN_BEGIN uint8_t Rx_Buff[ETH_RXBUFNB][ETH_RX_BUF_SIZE] __ALIGN_END;

#if defined(__ICCARM__) /*!< IAR Compiler */
#pragma data_alignment = 4
#endif
__ALIGN_BEGIN uint8_t Tx_Buff[ETH_TXBUFNB][ETH_TX_BUF_SIZE] __ALIGN_END;

extern ETH_HandleTypeDef heth;

static bHalBufList_t bMcuBufList = {.m_create = NULL, .m_next = NULL, .m_payload = NULL};

int bMcuEthInit(bHalBufList_t *pbuf_list)
{
    bMcuBufList.m_create  = pbuf_list->m_create;
    bMcuBufList.m_next    = pbuf_list->m_next;
    bMcuBufList.m_payload = pbuf_list->m_payload;
    /* Initialize Tx Descriptors list: Chain Mode */
    HAL_ETH_DMATxDescListInit(&heth, DMATxDscrTab, &Tx_Buff[0][0], ETH_TXBUFNB);
    /* Initialize Rx Descriptors list: Chain Mode  */
    HAL_ETH_DMARxDescListInit(&heth, DMARxDscrTab, &Rx_Buff[0][0], ETH_RXBUFNB);

    HAL_ETH_Start(&heth);
    return 0;
}

int bMcuEthGetMacAddr(uint8_t *paddr, uint8_t len)
{
    uint32_t tmpreg1;
    uint8_t  mac_addr[6];
    /* Load the selected MAC address high register */
    tmpreg1     = (*(__IO uint32_t *)((uint32_t)(ETH_MAC_ADDR_HBASE + ETH_MAC_ADDRESS0)));
    mac_addr[5] = (uint8_t)((tmpreg1 >> 8) & 0xff);
    mac_addr[4] = (uint8_t)((tmpreg1 >> 0) & 0xff);
    /* Load the selected MAC address low register */
    tmpreg1     = (*(__IO uint32_t *)((uint32_t)(ETH_MAC_ADDR_LBASE + ETH_MAC_ADDRESS0)));
    mac_addr[3] = (uint8_t)((tmpreg1 >> 24) & 0xff);
    mac_addr[2] = (uint8_t)((tmpreg1 >> 16) & 0xff);
    mac_addr[1] = (uint8_t)((tmpreg1 >> 8) & 0xff);
    mac_addr[0] = (uint8_t)((tmpreg1 >> 0) & 0xff);

    len = (len > 6) ? 6 : len;
    memcpy(paddr, mac_addr, len);
    return len;
}

uint8_t bMcuEthIsLinked()
{
    uint32_t regvalue = 0;
    HAL_ETH_ReadPHYRegister(&heth, PHY_BSR, &regvalue);
    regvalue &= PHY_LINKED_STATUS;
    return (regvalue != 0);
}

int bMcuEthReceive(void **pbuf, uint32_t *plen)
{
    void                    *buf         = NULL;
    void                    *payload     = NULL;
    uint32_t                 payload_len = 0;
    uint16_t                 len         = 0;
    uint8_t                 *buffer;
    __IO ETH_DMADescTypeDef *dmarxdesc;
    uint32_t                 bufferoffset    = 0;
    uint32_t                 payloadoffset   = 0;
    uint32_t                 byteslefttocopy = 0;
    uint32_t                 i               = 0;
    int                      retval          = -1;

    if (bMcuBufList.m_create == NULL)
    {
        return -1;
    }

    /* get received frame */
    if (HAL_ETH_GetReceivedFrame(&heth) != HAL_OK)

        return NULL;

    /* Obtain the size of the packet and put it into the "len" variable. */
    len    = heth.RxFrameInfos.length;
    buffer = (uint8_t *)heth.RxFrameInfos.buffer;

    if (len > 0)
    {
        /* We allocate a pbuf chain of pbufs from the Lwip buffer pool */
        bMcuBufList.m_create(len, &buf);
        if (buf != NULL)
        {
            *pbuf = buf;
            *plen = len;
            bMcuBufList.m_payload(buf, &payload, &payload_len);
        }
    }

    if (payload != NULL)
    {
        dmarxdesc    = heth.RxFrameInfos.FSRxDesc;
        bufferoffset = 0;
        do
        {
            byteslefttocopy = payload_len;
            payloadoffset   = 0;

            /* Check if the length of bytes to copy in current pbuf is bigger than Rx buffer size*/
            while ((byteslefttocopy + bufferoffset) > ETH_RX_BUF_SIZE)
            {
                /* Copy data to pbuf */
                memcpy((uint8_t *)((uint8_t *)payload + payloadoffset),
                       (uint8_t *)((uint8_t *)buffer + bufferoffset),
                       (ETH_RX_BUF_SIZE - bufferoffset));

                /* Point to next descriptor */
                dmarxdesc = (ETH_DMADescTypeDef *)(dmarxdesc->Buffer2NextDescAddr);
                buffer    = (uint8_t *)(dmarxdesc->Buffer1Addr);

                byteslefttocopy = byteslefttocopy - (ETH_RX_BUF_SIZE - bufferoffset);
                payloadoffset   = payloadoffset + (ETH_RX_BUF_SIZE - bufferoffset);
                bufferoffset    = 0;
            }
            /* Copy remaining data in pbuf */
            memcpy((uint8_t *)((uint8_t *)payload + payloadoffset),
                   (uint8_t *)((uint8_t *)buffer + bufferoffset), byteslefttocopy);
            bufferoffset = bufferoffset + byteslefttocopy;

            bMcuBufList.m_next(buf, &buf);
            if (buf != NULL)
            {
                bMcuBufList.m_payload(buf, &payload, &payload_len);
            }
        } while (buf != NULL);
    }

    /* Release descriptors to DMA */
    /* Point to first descriptor */
    dmarxdesc = heth.RxFrameInfos.FSRxDesc;
    /* Set Own bit in Rx descriptors: gives the buffers back to DMA */
    for (i = 0; i < heth.RxFrameInfos.SegCount; i++)
    {
        dmarxdesc->Status |= ETH_DMARXDESC_OWN;
        dmarxdesc = (ETH_DMADescTypeDef *)(dmarxdesc->Buffer2NextDescAddr);
    }

    /* Clear Segment_Count */
    heth.RxFrameInfos.SegCount = 0;

    /* When Rx Buffer unavailable flag is set: clear it and resume reception */
    if ((heth.Instance->DMASR & ETH_DMASR_RBUS) != (uint32_t)RESET)
    {
        /* Clear RBUS ETHERNET DMA flag */
        heth.Instance->DMASR = ETH_DMASR_RBUS;
        /* Resume DMA reception */
        heth.Instance->DMARPDR = 0;
    }
    return 0;
}

int bMcuEthTransmit(void *pbuf, uint32_t len)
{
    int                      errval;
    uint8_t                 *buffer = (uint8_t *)(heth.TxDesc->Buffer1Addr);
    __IO ETH_DMADescTypeDef *DmaTxDesc;
    uint32_t                 framelength     = 0;
    uint32_t                 bufferoffset    = 0;
    uint32_t                 byteslefttocopy = 0;
    uint32_t                 payloadoffset   = 0;
    DmaTxDesc                                = heth.TxDesc;
    bufferoffset                             = 0;

    void    *payload     = NULL;
    uint32_t payload_len = 0;
    /* copy frame from pbufs to driver buffers */
    if (bMcuBufList.m_payload == NULL)
    {
        return -1;
    }

    bMcuBufList.m_payload(pbuf, &payload, &payload_len);
    do
    {
        /* Is this buffer available? If not, goto error */
        if ((DmaTxDesc->Status & ETH_DMATXDESC_OWN) != (uint32_t)RESET)
        {
            errval = -1;
            goto error;
        }

        /* Get bytes in current lwIP buffer */
        byteslefttocopy = payload_len;
        payloadoffset   = 0;

        /* Check if the length of data to copy is bigger than Tx buffer size*/
        while ((byteslefttocopy + bufferoffset) > ETH_TX_BUF_SIZE)
        {
            /* Copy data to Tx buffer*/
            memcpy((uint8_t *)((uint8_t *)buffer + bufferoffset),
                   (uint8_t *)((uint8_t *)payload + payloadoffset),
                   (ETH_TX_BUF_SIZE - bufferoffset));

            /* Point to next descriptor */
            DmaTxDesc = (ETH_DMADescTypeDef *)(DmaTxDesc->Buffer2NextDescAddr);

            /* Check if the buffer is available */
            if ((DmaTxDesc->Status & ETH_DMATXDESC_OWN) != (uint32_t)RESET)
            {
                errval = -1;
                goto error;
            }

            buffer = (uint8_t *)(DmaTxDesc->Buffer1Addr);

            byteslefttocopy = byteslefttocopy - (ETH_TX_BUF_SIZE - bufferoffset);
            payloadoffset   = payloadoffset + (ETH_TX_BUF_SIZE - bufferoffset);
            framelength     = framelength + (ETH_TX_BUF_SIZE - bufferoffset);
            bufferoffset    = 0;
        }

        /* Copy the remaining bytes */
        memcpy((uint8_t *)((uint8_t *)buffer + bufferoffset),
               (uint8_t *)((uint8_t *)payload + payloadoffset), byteslefttocopy);
        bufferoffset = bufferoffset + byteslefttocopy;
        framelength  = framelength + byteslefttocopy;

        bMcuBufList.m_next(pbuf, &pbuf);
        if (pbuf != NULL)
        {
            bMcuBufList.m_payload(pbuf, &payload, &payload_len);
        }
    } while (pbuf != NULL);

    /* Prepare transmit descriptors to give to DMA */
    HAL_ETH_TransmitFrame(&heth, framelength);

    errval = 0;

error:
    /* When Transmit Underflow flag is set, clear it and issue a Transmit Poll Demand to resume
     * transmission */
    if ((heth.Instance->DMASR & ETH_DMASR_TUS) != (uint32_t)RESET)
    {
        /* Clear TUS ETHERNET DMA flag */
        heth.Instance->DMASR = ETH_DMASR_TUS;

        /* Resume DMA transmission*/
        heth.Instance->DMATPDR = 0;
    }
    return errval;
}

int bMcuEthLinkUpdate(uint8_t link_state)
{
    volatile uint32_t tickstart = 0;
    uint32_t          regvalue  = 0;

    if (link_state)
    {
        /* Restart the auto-negotiation */
        if (heth.Init.AutoNegotiation != ETH_AUTONEGOTIATION_DISABLE)
        {
            /* Enable Auto-Negotiation */
            HAL_ETH_WritePHYRegister(&heth, PHY_BCR, PHY_AUTONEGOTIATION);

            /* Get tick */
            tickstart = HAL_GetTick();

            /* Wait until the auto-negotiation will be completed */
            do
            {
                HAL_ETH_ReadPHYRegister(&heth, PHY_BSR, &regvalue);

                /* Check for the Timeout ( 1s ) */
                if ((HAL_GetTick() - tickstart) > 1000)
                {
                    /* In case of timeout */
                    goto error;
                }
            } while (((regvalue & PHY_AUTONEGO_COMPLETE) != PHY_AUTONEGO_COMPLETE));

            /* Read the result of the auto-negotiation */
            HAL_ETH_ReadPHYRegister(&heth, PHY_SR, &regvalue);

            /* Configure the MAC with the Duplex Mode fixed by the auto-negotiation process */
            if ((regvalue & PHY_DUPLEX_STATUS) != (uint32_t)RESET)
            {
                /* Set Ethernet duplex mode to Full-duplex following the auto-negotiation */
                heth.Init.DuplexMode = ETH_MODE_FULLDUPLEX;
            }
            else
            {
                /* Set Ethernet duplex mode to Half-duplex following the auto-negotiation */
                heth.Init.DuplexMode = ETH_MODE_HALFDUPLEX;
            }
            /* Configure the MAC with the speed fixed by the auto-negotiation process */
            if (regvalue & PHY_SPEED_STATUS)
            {
                /* Set Ethernet speed to 10M following the auto-negotiation */
                heth.Init.Speed = ETH_SPEED_10M;
            }
            else
            {
                /* Set Ethernet speed to 100M following the auto-negotiation */
                heth.Init.Speed = ETH_SPEED_100M;
            }
        }
        else /* AutoNegotiation Disable */
        {
        error:
            /* Check parameters */
            assert_param(IS_ETH_SPEED(heth.Init.Speed));
            assert_param(IS_ETH_DUPLEX_MODE(heth.Init.DuplexMode));

            /* Set MAC Speed and Duplex Mode to PHY */
            HAL_ETH_WritePHYRegister(
                &heth, PHY_BCR,
                ((uint16_t)(heth.Init.DuplexMode >> 3) | (uint16_t)(heth.Init.Speed >> 1)));
        }

        /* ETHERNET MAC Re-Configuration */
        HAL_ETH_ConfigMAC(&heth, (ETH_MACInitTypeDef *)NULL);

        /* Restart MAC interface */
        HAL_ETH_Start(&heth);
    }
    else
    {
        /* Stop MAC interface */
        HAL_ETH_Stop(&heth);
    }
    return 0;
}

#endif

#endif

#endif

/************************ Copyright (c) 2021 Bean *****END OF FILE****/
