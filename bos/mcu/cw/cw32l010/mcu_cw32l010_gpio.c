#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"

#define GPIO_REG_BASE   (0x48000000UL) 
#define GPIO_REG_OFF    (0x100UL)       //Port A/B 间隔为 0x100

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, pin)) return;

    // 1. 切换为数字模式 (0:数字, 1:模拟)
    if (pin == B_HAL_PINAll) pGpio->ANALOG = 0;
    else pGpio->ANALOG &= ~(1 << pin);

    // 2. 配置方向 (0:输入, 1:输出 - 注意：CW32底层DIR寄存器 0通常为输出，1为输入，请核对)
    // 此处按 CW32L010 标准：0-输出，1-输入
    if (dir == B_HAL_GPIO_OUTPUT) {
        if (pin == B_HAL_PINAll) pGpio->DIR = 0x0000;
        else pGpio->DIR &= ~(1 << pin);
    } else {
        if (pin == B_HAL_PINAll) pGpio->DIR = 0xFFFF;
        else pGpio->DIR |= (1 << pin);
    }

    // 3. 配置上下拉 (PUR:上拉, PDR寄存器在此结构体中未出现，CW32L010部分型号仅支持PUR)
    if (pin == B_HAL_PINAll) {
        pGpio->PUR = (pull == B_HAL_GPIO_PULLUP) ? 0xFFFF : 0x0000;
    } else {
        pGpio->PUR &= ~(1 << pin);
        if (pull == B_HAL_GPIO_PULLUP) pGpio->PUR |= (1 << pin);
    }
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, pin)) return;

    if (pin == B_HAL_PINAll) {
        pGpio->ODR = (s) ? 0xFFFF : 0x0000;
    } else {
        // 使用 BSRR 寄存器提高效率
        // BSRR 低16位为 Set (BSSx), 高16位为 Reset (BRRx)
        if (s) pGpio->BSRR = (1 << pin);
        else   pGpio->BSRR = (1 << (pin + 16));
    }
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, pin) || pin == B_HAL_PINAll) return 0;

    return ((pGpio->IDR >> pin) & 0x01);
}


