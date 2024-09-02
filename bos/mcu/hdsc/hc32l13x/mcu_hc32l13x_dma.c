#include "b_config.h"
#include "hal/inc/b_hal_dma.h"

#if (defined(HC32L13X))

typedef struct
{
    volatile uint32_t CONF;
    uint8_t           RESERVED1[12];
    struct
    {
        volatile uint32_t CONFA;
        volatile uint32_t CONFB;
        volatile uint32_t SRCADR;
        volatile uint32_t DSTADR;
    } chl[2];
} McuDmaReg_t;

#define DMA1_BASE_ADDR (0x40021000UL)
int bMcuDmaConfig(bHalDmaConfig_t *pconf)
{
    if (pconf->chl > B_HAL_DMA_CHL_2)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA1_BASE_ADDR);
    if (B_READ_BIT(pdma->CONF, (uint32_t)0x1 << 31) == 0)
    {
        B_SET_BIT(pdma->CONF, (uint32_t)0x1 << 31);
    }
    if (pconf->request == B_DMA_REQ_UART1_RX)
    {
        B_MODIFY_REG(pdma->chl[pconf->chl].CONFA, 0x1F << 23, 0x28 << 23);
    }
    else if (pconf->request == B_DMA_REQ_UART2_RX)
    {
        B_MODIFY_REG(pdma->chl[pconf->chl].CONFA, 0x1F << 23, 0x2A << 23);
    }
    else if (pconf->request == B_DMA_REQ_LPUART1_RX)
    {
        B_MODIFY_REG(pdma->chl[pconf->chl].CONFA, 0x1F << 23, 0x2C << 23);
    }
    else if (pconf->request == B_DMA_REQ_LPUART2_RX)
    {
        B_MODIFY_REG(pdma->chl[pconf->chl].CONFA, 0x1F << 23, 0x2E << 23);
    }
    else
    {
        return -1;
    }
    B_MODIFY_REG(pdma->chl[pconf->chl].CONFA, 0xF << 16, 0 << 16);
    B_MODIFY_REG(pdma->chl[pconf->chl].CONFA, 0xFFFF << 0, (pconf->count - 1) << 0);

    B_CLEAR_BIT(pdma->chl[pconf->chl].CONFB, 0x3 << 28);
    B_MODIFY_REG(pdma->chl[pconf->chl].CONFB, 0x3 << 26, (pconf->bits) << 26);
    if (pconf->inc == B_DMA_SRC_ADDR_INC)
    {
        B_CLEAR_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 25);
        B_SET_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 24);
    }
    else if (pconf->inc == B_DMA_DEST_ADDR_INC)
    {
        B_SET_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 25);
        B_CLEAR_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 24);
    }
    else if (pconf->inc == B_DMA_BOTH_INC)
    {
        B_CLEAR_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 25);
        B_CLEAR_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 24);
    }
    else
    {
        B_SET_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 25);
        B_SET_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 24);
    }
    B_SET_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 21);
    B_SET_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 22);
    B_SET_BIT(pdma->chl[pconf->chl].CONFB, 0x1 << 23);
    B_WRITE_REG(pdma->chl[pconf->chl].DSTADR, pconf->dest);
    B_WRITE_REG(pdma->chl[pconf->chl].SRCADR, pconf->src);
    return 0;
}

int bMcuDmaStart(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_2)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA1_BASE_ADDR);
    B_SET_BIT(pdma->chl[chl].CONFA, ((uint32_t)0x1 << 31));
    return 0;
}

int bMcuDmaStop(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_2)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA1_BASE_ADDR);
    B_CLEAR_BIT(pdma->chl[chl].CONFA, ((uint32_t)0x1 << 31));
    return 0;
}

int bMcuDmaSetDest(bHalDmaChlNumber_t chl, uint32_t addr)
{
    if (chl > B_HAL_DMA_CHL_2)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA1_BASE_ADDR);
    B_WRITE_REG(pdma->chl[chl].DSTADR, addr);
    return 0;
}

int bMcuDmaSetCount(bHalDmaChlNumber_t chl, uint32_t count)
{
    if (chl > B_HAL_DMA_CHL_2)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA1_BASE_ADDR);
    B_MODIFY_REG(pdma->chl[chl].CONFA, 0xFFFF << 0, (count - 1) << 0);
    return 0;
}

int bMcuDmaGetCount(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_2)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA1_BASE_ADDR);
    return ((pdma->chl[chl].CONFA & 0xffff) + 1);
}

#endif
