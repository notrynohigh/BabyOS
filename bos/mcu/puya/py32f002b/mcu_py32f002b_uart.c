#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

#if !defined(PY32F002Bx5)
/* 根据您的工程具体 target 定义来调整 */
#endif

#include "py32f0xx.h"

/* PY32F002B 资源较少，通常仅有 USART1 */
static USART_TypeDef *bMcuUartTable[] = {USART1};

/**
 * @brief 串口发送函数 (轮询方式)
 * @param uart 串口号
 * @param pbuf 数据缓冲区
 * @param len  数据长度
 */
int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    /* PY32F002B 常用封装通常只有 B_HAL_UART_1 */
    if (uart == B_HAL_UART_1)
    {
        for (uint16_t i = 0; i < len; i++)
        {
            /* 等待发送数据寄存器为空 (TXE) */
            while (!(bMcuUartTable[uart]->SR & USART_SR_TXE))
                ;
            /* 装载数据到发送寄存器 */
            bMcuUartTable[uart]->DR = pbuf[i];
        }
        return 0;
    }
    return -1;
}

/**
 * @brief 由于 PY32F002B 硬件不支持 DMA，此函数返回错误或改为中断初始化
 * @note 建议在外部框架层改用中断方式接收
 */
int bMcuUartReceiveDma(bHalUartNumber_t uart, bHalDmaConfig_t *pconf)
{
    /* 硬件无 DMA 模块，直接返回不支持 */
    return -1;
}

/**
 * @brief (补充) 串口接收中断配置
 * 如果您的框架需要接收数据，建议实现一个类似这样的中断使能函数
 */
void bMcuUartEnableRxIt(bHalUartNumber_t uart)
{
    if (uart == B_HAL_UART_1)
    {
        /* 使能接收缓冲区非空中断 (RXNE) */
        bMcuUartTable[uart]->CR1 |= USART_CR1_RXNEIE;
        
        /* 别忘了在 NVIC 中开启 USART1 中断 */
        NVIC_EnableIRQ(USART1_IRQn);
    }
}

