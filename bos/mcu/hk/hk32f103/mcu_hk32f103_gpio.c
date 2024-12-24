#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"

#define GPIO_REG_OFF (0x400UL)
#define GPIO_REG_BASE (0x40010800UL)

typedef struct
{
    volatile uint32_t CRL;
    volatile uint32_t CRH;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t BRR;
    volatile uint32_t LCKR;
} McuGpioReg_t;

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    uint32_t      dir_val  = 4;
    uint32_t      pull_val = 0;
    McuGpioReg_t *pGpio    = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);

    if (!B_HAL_GPIO_ISVALID(port, pin))
    {
        return;
    }

    if (dir == B_HAL_GPIO_OUTPUT)
    {
        dir_val = (pin == B_HAL_PINAll) ? 0x33333333 : 3;
    }
    else if (pull != B_HAL_GPIO_NOPULL)
    {
        dir_val  = (pin == B_HAL_PINAll) ? 0x88888888 : 8;
        pull_val = (pin == B_HAL_PINAll) ? 0xFFFF : (0X0001 << pin);

        if (pull == B_HAL_GPIO_PULLUP)
        {
            pGpio->BSRR = pull_val;
        }
        else
        {
            pGpio->BRR = pull_val;
        }
    }

    if (pin == B_HAL_PINAll)
    {
        pGpio->CRL = dir_val;
        pGpio->CRH = dir_val;
    }
    else
    {
        if (pin < B_HAL_PIN8)
        {
            pGpio->CRL &= ~(0x0000000F << (pin * 4));
            pGpio->CRL |= (dir_val << (pin * 4));
        }
        else
        {
            pGpio->CRH &= ~(0x0000000F << ((pin - B_HAL_PIN8) * 4));
            pGpio->CRH |= (dir_val << ((pin - B_HAL_PIN8) * 4));
        }
    }
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    uint32_t      cs_val = 0x00000001 << pin;
    McuGpioReg_t *pGpio  = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, pin) || pin == B_HAL_PINAll)
    {
        return;
    }
    if (s == 0)
    {
        cs_val <<= 16;
    }
    pGpio->BSRR = cs_val;
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    uint32_t      id_val = 0;
    McuGpioReg_t *pGpio  = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, pin) || pin == B_HAL_PINAll)
    {
        return 0;
    }
    id_val = pGpio->IDR;
    return ((id_val & (0x0001 << pin)) != 0);
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
    uint32_t      id_val = 0;
    McuGpioReg_t *pGpio  = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, 0))
    {
        return 0;
    }
    id_val = pGpio->IDR;
    return (id_val & 0xffff);
}
