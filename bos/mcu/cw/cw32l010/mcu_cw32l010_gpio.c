#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"
#include "cw32l010.h"

/* 根据官方定义：GPIOA 基地址 0x48000000, 端口偏移 0x100 (Size=0x64, 通常间隔为 0x100) */
#define GPIO_REG_OFF  (0x100UL)
#define GPIO_REG_BASE (0x48000000UL)

/* 严格按照你提供的官方结构体进行定义 */
typedef struct {
    union { __IOM uint32_t DIR;       struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } DIR_f; }; // 0x00
    union { __IOM uint32_t OPENDRAIN; struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } OPENDRAIN_f; }; // 0x04
    __IM  uint32_t RESERVED[2];                                                                                              // 0x08-0x0F
    union { __IOM uint32_t PUR;       struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } PUR_f; }; // 0x10
    union { __IOM uint32_t AFRH;      struct { __IOM uint32_t AFR8:3; } AFRH_f; };                                           // 0x14
    union { __IOM uint32_t AFRL;      struct { __IOM uint32_t AFR0:3, :1, AFR1:3, :1, AFR2:3, :1, AFR3:3, :1, AFR4:3, :1, AFR5:3, :1, AFR6:3, :1, AFR7:3; } AFRL_f; }; // 0x18
    union { __IOM uint32_t ANALOG;    struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } ANALOG_f; }; // 0x1C
    __IM  uint32_t RESERVED1;                                                                                                // 0x20
    union { __IOM uint32_t RISEIE;    struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } RISEIE_f; }; // 0x24
    union { __IOM uint32_t FALLIE;    struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } FALLIE_f; }; // 0x28
    __IM  uint32_t RESERVED2[2];                                                                                             // 0x2C-0x33
    union { __IOM uint32_t ISR;       struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } ISR_f; };    // 0x34
    union { __IOM uint32_t ICR;       struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } ICR_f; };    // 0x38
    __IM  uint32_t RESERVED3;                                                                                                // 0x3C
    union { __IOM uint32_t FILTER;    struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1, :7, FLTCLK:3; } FILTER_f; }; // 0x40
    __IM  uint32_t RESERVED4[3];                                                                                             // 0x44-0x4F
    union { __IOM uint32_t IDR;       struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } IDR_f; };    // 0x50
    union { __IOM uint32_t ODR;       struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } ODR_f; };    // 0x54
    union { __IOM uint32_t BRR;       struct { __IOM uint32_t BRR0:1, BRR1:1, BRR2:1, BRR3:1, BRR4:1, BRR5:1, BRR6:1, BRR7:1, BRR8:1; } BRR_f; };    // 0x58
    union { __IOM uint32_t BSRR;      struct { __IOM uint32_t BSS0:1, BSS1:1, BSS2:1, BSS3:1, BSS4:1, BSS5:1, BSS6:1, BSS7:1, BSS8:1, :7, BRR0:1, BRR1:1, BRR2:1, BRR3:1, BRR4:1, BRR5:1, BRR6:1, BRR7:1, BRR8:1; } BSRR_f; }; // 0x5C
    union { __IOM uint32_t TOG;       struct { __IOM uint32_t PIN0:1, PIN1:1, PIN2:1, PIN3:1, PIN4:1, PIN5:1, PIN6:1, PIN7:1, PIN8:1; } TOG_f; };    // 0x60
} McuGpioReg_t;

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    uint32_t pin_mask = (pin == B_HAL_PINAll) ? 0x1FF : (0x0001 << pin);

    if (!B_HAL_GPIO_ISVALID(port, pin))
    {
        return;
    }

    // 1. 关闭模拟功能 (设置为数字模式)
    pGpio->ANALOG &= ~pin_mask;

    // 2. 配置方向 (DIR: 0-输出, 1-输入)
    if (dir == B_HAL_GPIO_OUTPUT)
    {
        pGpio->DIR &= ~pin_mask;
    }
    else
    {
        pGpio->DIR |= pin_mask;
    }

    // 3. 配置上下拉 (PUR寄存器控制上拉，CW32L010通常只有上拉配置)
    if (pull == B_HAL_GPIO_PULLUP)
    {
        pGpio->PUR |= pin_mask;
    }
    else
    {
        pGpio->PUR &= ~pin_mask;
    }
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    
    if (!B_HAL_GPIO_ISVALID(port, pin) || pin == B_HAL_PINAll)
    {
        return;
    }

    if (s)
    {
        // 使用 BSRR 的 BSSx 位（低16位）置位
        pGpio->BSRR = (0x0001 << pin);
    }
    else
    {
        // 使用 BRR 寄存器清零
        pGpio->BRR = (0x0001 << pin);
    }
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    McuGpioReg_t *pGpio = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    
    if (!B_HAL_GPIO_ISVALID(port, pin) || pin == B_HAL_PINAll)
    {
        return 0;
    }

    return ((pGpio->IDR & (0x0001 << pin)) ? 1 : 0);
}