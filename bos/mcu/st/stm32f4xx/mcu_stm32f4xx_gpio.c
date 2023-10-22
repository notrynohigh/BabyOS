#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"

#define GPIO_REG_OFF (0x400UL)
#define GPIO_REG_BASE (0x40020000UL)

typedef struct
{
    volatile uint32_t MODER;   /*!< GPIO port mode register */
    volatile uint32_t OTYPER;  /*!< GPIO port output type register */
    volatile uint32_t OSPEEDR; /*!< GPIO port output speed register */
    volatile uint32_t PUPDR;   /*!< GPIO port pull-up/pull-down register */
    volatile uint32_t IDR;     /*!< GPIO port input data register */
    volatile uint32_t ODR;     /*!< GPIO port output data register */
    volatile uint32_t BSRR;    /*!< GPIO port bit set/reset register */
    volatile uint32_t LCKR;    /*!< GPIO port configuration lock register */
    volatile uint32_t AFR[2];  /*!< GPIO alternate function registers */
} McuGpioReg_t;

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    uint32_t      mode_val   = 0;
    uint32_t      otype_val  = 0;
    uint32_t      ospeed_val = 0;
    uint32_t      pupd_val   = 0;
    McuGpioReg_t *pGpio      = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);

    if (!B_HAL_GPIO_ISVALID(port, pin))
    {
        return;
    }

    if (dir == B_HAL_GPIO_OUTPUT)
    {
        mode_val   = (pin == B_HAL_PINAll) ? 0x55555555 : 1;
        otype_val  = (pin == B_HAL_PINAll) ? 0x00000000 : 0;
        ospeed_val = (pin == B_HAL_PINAll) ? 0xffffffff : 3;
        pupd_val   = (pin == B_HAL_PINAll) ? 0x00000000 : 0;
    }

    if (pull != B_HAL_GPIO_NOPULL)
    {
        mode_val = (pin == B_HAL_PINAll) ? 0x00000000 : 0;
        if (pull == B_HAL_GPIO_PULLUP)
        {
            pupd_val = (pin == B_HAL_PINAll) ? 0x55555555 : 1;
        }
        else
        {
            pupd_val = (pin == B_HAL_PINAll) ? 0xAAAAAAAA : 2;
        }
    }

    if (pin == B_HAL_PINAll)
    {
        pGpio->MODER   = mode_val;
        pGpio->OTYPER  = otype_val;
        pGpio->OSPEEDR = ospeed_val;
        pGpio->PUPDR  = pupd_val;
    }
    else
    {
        pGpio->MODER &= ~(0x00000003 << (pin * 2));
        pGpio->MODER |= (mode_val << (pin * 2));
        pGpio->OTYPER &= ~(0x00000001 << (pin * 1));
        pGpio->OTYPER |= (otype_val << (pin * 1));
        pGpio->OSPEEDR &= ~(0x00000003 << (pin * 2));
        pGpio->OSPEEDR |= (ospeed_val << (pin * 2));
        pGpio->PUPDR &= ~(0x00000003 << (pin * 2));
        pGpio->PUPDR |= (pupd_val << (pin * 2));
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

void bMcuGpioWritePort(bHalGPIOPort_t port, uint16_t dat)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    if (!B_HAL_GPIO_ISVALID(port, 0))
    {
        return;
    }
    pGpio->ODR = dat;
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
