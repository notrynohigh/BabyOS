/**
 *!
 * \file        mcu_stm32g0b1_i2c.c
 * \version     v0.0.1
 * \date        2025/12/25
 * \author      Haimeng(Haimeng.chen@qq.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 Haimeng
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SI2CL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_i2c.h"

#if (defined(STM32G0B1))

#include "stm32g0xx_hal.h"

extern I2C_HandleTypeDef hi2c1;


#define I2C1_BASE_ADDR (0x40005400)
#define I2C2_BASE_ADDR (0x40005800)
#define I2C3_BASE_ADDR (0x40008800)

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t TIMINGR;
    volatile uint32_t TIMEOUTR;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t PECR;
    volatile uint32_t RXDR;
    volatile uint32_t TXDR;
} McuI2cReg_t;

#define MCU_I2C1 ((McuI2cReg_t *)I2C1_BASE_ADDR)
#define MCU_I2C2 ((McuI2cReg_t *)I2C2_BASE_ADDR)
#define MCU_I2C3 ((McuI2cReg_t *)I2C3_BASE_ADDR)

//static McuI2cReg_t *I2cTable[3] = {MCU_I2C1, MCU_I2C2, MCU_I2C3};


int bMcuI2CReadByte(const bHalI2CIf_t *i2c_if, uint8_t *pbuf, uint16_t len)
{
	if (IS_NULL(i2c_if) || (IS_NULL(pbuf) && (len > 0)))
    {
        return -1;
    }
    return -1;
}

int bMcuI2CWriteByte(const bHalI2CIf_t *i2c_if, uint8_t *pbuf, uint16_t len)
{
	int ret_val = -1;
    if (IS_NULL(i2c_if) || (IS_NULL(pbuf) && (len > 0)))
    {
        return -1;
    }
	ret_val = HAL_I2C_Master_Transmit(&hi2c1,i2c_if->dev_addr , pbuf, len, 1000);
    return ret_val;
}

int bMcuI2CMemWrite(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                    const uint8_t *pbuf, uint16_t len)
{
	int ret_val = -1;
    if (IS_NULL(i2c_if) || (IS_NULL(pbuf) && (len > 0)))
    {
        return -1;
    }
	ret_val = HAL_I2C_Mem_Write(&hi2c1,i2c_if->dev_addr , mem_addr, mem_addr_size, (uint8_t *)pbuf, len, 1000);
    return ret_val;
}

int bMcuI2CMemRead(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                   uint8_t *pbuf, uint16_t len)
{
	int ret_val = -1;
    if (IS_NULL(i2c_if) || (IS_NULL(pbuf) && (len > 0)))
    {
        return -1;
    }
	ret_val = HAL_I2C_Mem_Read(&hi2c1, i2c_if->dev_addr, mem_addr, mem_addr_size, pbuf, len, 1000);
    return ret_val;
}

#endif

/************************ Copyright (c) 2025 Haimeng *****END OF FILE****/
