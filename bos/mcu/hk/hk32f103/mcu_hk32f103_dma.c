#include "b_config.h"
#include "hal/inc/b_hal_dma.h"

#if !defined(HK32F103C8XX) && !defined(HK32F103CBXX) && !defined(HK32F103R8XX) && \
    !defined(HK32F103RBXX) && !defined(HK32F103V8XX) && !defined(HK32F103VBXX) && \
    !defined(HK32F103RCXX) && !defined(HK32F103RDXX) && !defined(HK32F103REXX) && \
    !defined(HK32F103VCXX) && !defined(HK32F103VDXX) && !defined(HK32F103VEXX)

#error \
    "Please select first the target HK32F10x device used in your application (in HK32f10x.h file)"

#else

#include "hk32f10x.h"

#define DMA1_CLOCK_ENABLE() B_SET_BIT((*((volatile uint32_t *)(0x40021000 + 0x14))), 0x1)

static DMA_Channel_TypeDef *bMcuDmaChlTable[] = {DMA1_Channel1, DMA1_Channel2, DMA1_Channel3,
                                                 DMA1_Channel4, DMA1_Channel5, DMA1_Channel6,
                                                 DMA1_Channel7};

int bMcuDmaConfig(bHalDmaConfig_t *pconf)
{
    if (pconf->chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    DMA1_CLOCK_ENABLE();

    DMA_InitTypeDef DMA_InitStruct;

    if (pconf->request == B_DMA_REQ_UART1_RX || pconf->request == B_DMA_REQ_UART2_RX ||
        pconf->request == B_DMA_REQ_LPUART1_RX)
    {
        DMA_InitStruct.DMA_PeripheralBaseAddr = pconf->src;
        DMA_InitStruct.DMA_MemoryBaseAddr     = pconf->dest;
        DMA_InitStruct.DMA_DIR                = DMA_DIR_PeripheralSRC;
        DMA_InitStruct.DMA_BufferSize         = pconf->count;
        DMA_InitStruct.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
        DMA_InitStruct.DMA_MemoryInc          = DMA_MemoryInc_Enable;
        DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
        DMA_InitStruct.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
        DMA_InitStruct.DMA_Mode     = (pconf->is_circular) ? DMA_Mode_Circular : DMA_Mode_Normal;
        DMA_InitStruct.DMA_Priority = DMA_Priority_High;
    }

    DMA_Init(bMcuDmaChlTable[pconf->chl], &DMA_InitStruct);
    return 0;
}

int bMcuDmaStart(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    DMA_Cmd(bMcuDmaChlTable[chl], ENABLE);
    return 0;
}

int bMcuDmaStop(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    DMA_Cmd(bMcuDmaChlTable[chl], DISABLE);
    return 0;
}

int bMcuDmaSetDest(bHalDmaChlNumber_t chl, uint32_t addr)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }

    if (B_READ_BIT(bMcuDmaChlTable[chl]->CCR, 0x1 << 4) == 0)
    {
        bMcuDmaChlTable[chl]->CMAR = addr;
    }
    else
    {
        bMcuDmaChlTable[chl]->CPAR = addr;
    }
    return 0;
}

int bMcuDmaSetCount(bHalDmaChlNumber_t chl, uint32_t count)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    DMA_SetCurrDataCounter(bMcuDmaChlTable[chl], count);
    return 0;
}

int bMcuDmaGetCount(bHalDmaChlNumber_t chl)
{
    if (chl > B_HAL_DMA_CHL_7)
    {
        return -1;
    }
    return DMA_GetCurrDataCounter(bMcuDmaChlTable[chl]);
}

#endif
