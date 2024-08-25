#include "b_config.h"
#include "hal/inc/b_hal_rng.h"

typedef struct
{
    volatile uint32_t CR; /*!< RNG control register, Address offset: 0x00 */
    volatile uint32_t SR; /*!< RNG status register,  Address offset: 0x04 */
    volatile uint32_t DR; /*!< RNG data register,    Address offset: 0x08 */
} McuRNGReg_t;

#define RNG_BASE_ADDR (0x40025000UL)
#define MCU_RNG ((McuRNGReg_t *)RNG_BASE_ADDR)

uint32_t bMcuRNGRead()
{
    McuRNGReg_t *rng = MCU_RNG;
    if ((rng->CR & (0x1 << 2)) == 0)
    {
        rng->CR |= (0x1 << 2);
    }
    while ((rng->SR & 0x1) == 0)
    {
        (void)0;
    }
    return rng->DR;
}

