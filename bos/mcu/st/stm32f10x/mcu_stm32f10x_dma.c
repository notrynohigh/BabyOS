#include "b_config.h"
#include "hal/inc/b_hal_dma.h"

typedef struct
{
    volatile uint32_t CCR;   /*!< DMA channel x configuration register */
    volatile uint32_t CNDTR; /*!< DMA channel x number of data register */
    volatile uint32_t CPAR;  /*!< DMA channel x peripheral address register */
    volatile uint32_t CMAR;  /*!< DMA channel x memory address register */
} McuDmaReg_t;

#define DMA1_BASE_ADDR (0x40020000)

#define DMA_CHL_BASE (DMA1_BASE_ADDR + 0x00000008UL)
#define DMA1_CLOCK_ENABLE() B_SET_BIT((*((volatile uint32_t *)(0x40021000 + 0x14))), 0x1)

int bMcuDmaConfig(bHalDmaConfig_t *pconf)
{
    if (pconf->chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    DMA1_CLOCK_ENABLE();
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA_CHL_BASE + pconf->chl * 0x14);

    if (pconf->request == B_DMA_REQ_UART1_RX || pconf->request == B_DMA_REQ_UART2_RX ||
        pconf->request == B_DMA_REQ_LPUART1_RX)
    {
        B_CLEAR_BIT(pdma->CCR, (0x1 << 14));
        B_CLEAR_BIT(pdma->CCR, (0x1 << 4));
        if (pconf->inc == B_DMA_DEST_ADDR_INC)
        {
            B_SET_BIT(pdma->CCR, (0x1 << 7));    // memory
            B_CLEAR_BIT(pdma->CCR, (0x1 << 6));  // peripheral
        }
        pdma->CPAR = pconf->src;
        pdma->CMAR = pconf->dest;
    }
    if (pconf->bits == B_DMA_DATA_BIT8)
    {
        B_MODIFY_REG(pdma->CCR, 0x3 << 8, 0x0);
        B_MODIFY_REG(pdma->CCR, 0x3 << 10, 0x0);
    }
    else if (pconf->bits == B_DMA_DATA_BIT16)
    {
        B_MODIFY_REG(pdma->CCR, 0x3 << 8, 0x1);
        B_MODIFY_REG(pdma->CCR, 0x3 << 10, 0x1);
    }
    else if (pconf->bits == B_DMA_DATA_BIT32)
    {
        B_MODIFY_REG(pdma->CCR, 0x3 << 8, 0x2);
        B_MODIFY_REG(pdma->CCR, 0x3 << 10, 0x2);
    }
    B_MODIFY_REG(pdma->CCR, 0x3 << 12, 0x2);
    if (pconf->inc == B_DMA_BOTH_INC)
    {
        B_SET_BIT(pdma->CCR, (0x1 << 7));
        B_SET_BIT(pdma->CCR, (0x1 << 6));
    }
    else if (pconf->inc == B_DMA_NONE_INC)
    {
        B_CLEAR_BIT(pdma->CCR, (0x1 << 7));
        B_CLEAR_BIT(pdma->CCR, (0x1 << 6));
    }
    if (pconf->is_circular)
    {
        B_SET_BIT(pdma->CCR, (0x1 << 5));
    }
    else
    {
        B_CLEAR_BIT(pdma->CCR, (0x1 << 5));
    }
    pdma->CNDTR = pconf->count;
    return 0;
}

int bMcuDmaStart(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA_CHL_BASE + chl * 0x14);
    B_SET_BIT(pdma->CCR, (0x1 << 0));
    return 0;
}

int bMcuDmaStop(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA_CHL_BASE + chl * 0x14);
    B_CLEAR_BIT(pdma->CCR, (0x1 << 0));
    return 0;
}

int bMcuDmaSetDest(bHalDmaChlNumber_t chl, uint32_t addr)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA_CHL_BASE + chl * 0x14);
    if (B_READ_BIT(pdma->CCR, 0x1 << 4) == 0)
    {
        pdma->CMAR = addr;
    }
    else
    {
        pdma->CPAR = addr;
    }
    return 0;
}

int bMcuDmaSetCount(bHalDmaChlNumber_t chl, uint32_t count)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA_CHL_BASE + chl * 0x14);
    pdma->CNDTR       = count;
    return 0;
}

int bMcuDmaGetCount(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    McuDmaReg_t *pdma = (McuDmaReg_t *)(DMA_CHL_BASE + chl * 0x14);
    return pdma->CNDTR;
}
