#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"

/* PY32F002B GPIO 基地址定义 */
#define GPIO_REG_BASE (0x50000000UL)
#define GPIO_REG_OFF  (0x400UL)

/* 适配 PY32F0 系列的寄存器结构 */
typedef struct
{
    volatile uint32_t MODER;   /* 模式寄存器 (每引脚 2 bit) */
    volatile uint32_t OTYPER;  /* 输出类型寄存器 (每引脚 1 bit) */
    volatile uint32_t OSPEEDR; /* 输出速度寄存器 (每引脚 2 bit) */
    volatile uint32_t PUPDR;   /* 上下拉寄存器 (每引脚 2 bit) */
    volatile uint32_t IDR;     /* 输入数据寄存器 */
    volatile uint32_t ODR;     /* 输出数据寄存器 */
    volatile uint32_t BSRR;    /* 置位/复位寄存器 */
    volatile uint32_t LCKR;    /* 锁定寄存器 */
    volatile uint32_t AFRL;    /* 复用功能低位寄存器 */
    volatile uint32_t AFRH;    /* 复用功能高位寄存器 */
    volatile uint32_t BRR;     /* 位复位寄存器 */
} McuGpioReg_t;

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    uint32_t moder = 0, pupdr = 0;

    if (!B_HAL_GPIO_ISVALID(port, pin))
    {
        return;
    }

    /* 1. 配置方向 (MODER) */
    if (dir == B_HAL_GPIO_OUTPUT)
    {
        moder = 1; // General purpose output mode
    }
    else
    {
        moder = 0; // Input mode
    }

    /* 2. 配置上下拉 (PUPDR) */
    if (pull == B_HAL_GPIO_PULLUP)
    {
        pupdr = 1;
    }
    else if (pull == B_HAL_GPIO_PULLDOWN)
    {
        pupdr = 2;
    }
    else
    {
        pupdr = 0;
    }

    if (pin == B_HAL_PINAll)
    {
        pGpio->MODER = (dir == B_HAL_GPIO_OUTPUT) ? 0x55555555 : 0x00000000;
        pGpio->PUPDR = (pull == B_HAL_GPIO_PULLUP) ? 0x55555555 : 
                       (pull == B_HAL_GPIO_PULLDOWN ? 0xAAAAAAAA : 0);
    }
    else
    {
        /* 清除并设置 MODER (2 bits per pin) */
        pGpio->MODER &= ~(0x3 << (pin * 2));
        pGpio->MODER |= (moder << (pin * 2));

        /* 清除并设置 PUPDR (2 bits per pin) */
        pGpio->PUPDR &= ~(0x3 << (pin * 2));
        pGpio->PUPDR |= (pupdr << (pin * 2));
    }
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, pin) || pin == B_HAL_PINAll)
    {
        return;
    }
    
    if (s != 0)
    {
        pGpio->BSRR = (1U << pin);
    }
    else
    {
        pGpio->BSRR = (1U << (pin + 16)); // 高 16 位用于清除
    }
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, pin) || pin == B_HAL_PINAll)
    {
        return 0;
    }
    return ((pGpio->IDR & (1U << pin)) != 0);
}

void bMcuGpioWritePort(bHalGPIOPort_t port, uint16_t dat)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, 0))
    {
        return;
    }
    pGpio->ODR = dat;
}

uint16_t bMcuGpioReadPort(bHalGPIOPort_t port)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, 0))
    {
        return 0;
    }
    return (uint16_t)(pGpio->IDR & 0xFFFF);
}

