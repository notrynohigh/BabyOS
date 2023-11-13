/**
 *!
 * \file        b_hal_i2c.c
 * \version     v0.0.1
 * \date        2020/03/25
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SUARTL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
/*Includes ----------------------------------------------*/
#include "hal/inc/b_hal.h"
/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup I2C
 * \{
 */

/**
 * \defgroup I2C_Private_TypesDefinitions
 */
typedef struct
{
    bHalGPIOInstance_t clk;
    bHalGPIOInstance_t sda;
    uint32_t           frq;
} bHalI2CIO_t;
/**
 * \}
 */

/**
 * \addtogroup I2C_Private_Functions
 * \{
 */
// 计算I2C延时微秒数值的函数
static uint32_t _HalCalculateI2CDelayUs(uint32_t i2cFrequency)
{
    if (i2cFrequency <= 100000)
    {
        // Standard Mode (100kHz)
        return 5;
    }
    else if (i2cFrequency <= 200000)
    {
        return 2;
    }
    else if (i2cFrequency <= 400000)
    {
        // Fast Mode (400kHz)
        return 1;
    }
    else if (i2cFrequency <= 1000000)
    {
        // Fast Mode Plus (1MHz)
        return 1;
    }
    else
    {
        return 1;
    }
}

static void _HalI2CIOStart(bHalI2CIO_t i2c)
{
    uint32_t us = _HalCalculateI2CDelayUs(i2c.frq);
    bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 1);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 1);
    bHalDelayUs(us);
    bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 0);
    bHalDelayUs(us);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
}

static void _HalI2CIOStop(bHalI2CIO_t i2c)
{
    uint32_t us = _HalCalculateI2CDelayUs(i2c.frq);
    bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 0);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 1);
    bHalDelayUs(us);
    bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 1);
    bHalDelayUs(us);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
    bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 0);
}

static int _HalI2CIOACK(bHalI2CIO_t i2c)
{
    uint8_t tmp = 0xff;
    uint32_t us  = _HalCalculateI2CDelayUs(i2c.frq);
    bHalGpioConfig(i2c.sda.port, i2c.sda.pin, B_HAL_GPIO_INPUT, B_HAL_GPIO_NOPULL);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
    bHalDelayUs(us);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 1);
    do
    {
        tmp--;
        bHalDelayUs(10 * us);
    } while ((bHalGpioReadPin(i2c.sda.port, i2c.sda.pin) != 0x0) && (tmp > 0));
    if (tmp == 0)
    {
        _HalI2CIOStop(i2c);
        bHalGpioConfig(i2c.sda.port, i2c.sda.pin, B_HAL_GPIO_OUTPUT, B_HAL_GPIO_NOPULL);
        return -1;
    }
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
    bHalGpioConfig(i2c.sda.port, i2c.sda.pin, B_HAL_GPIO_OUTPUT, B_HAL_GPIO_NOPULL);
    return 0;
}

static void _HalI2CIOmACK(bHalI2CIO_t i2c)
{
    uint32_t us = _HalCalculateI2CDelayUs(i2c.frq);
    bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 0);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 1);
    bHalDelayUs(us);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
    bHalDelayUs(us);
}

static void _HalI2CIOWriteByte(bHalI2CIO_t i2c, uint8_t dat)
{
    uint8_t  i  = 0;
    uint32_t us = _HalCalculateI2CDelayUs(i2c.frq);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
    for (i = 0; i < 8; i++)
    {
        bHalDelayUs(us);
        if (dat & 0x80)
        {
            bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 1);
        }
        else
        {
            bHalGpioWritePin(i2c.sda.port, i2c.sda.pin, 0);
        }
        bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 1);
        bHalDelayUs(us);
        bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
        dat <<= 1;
    }
}

static uint8_t _HalI2CIOReadByte(bHalI2CIO_t i2c)
{
    uint8_t  i = 0, tmp = 0;
    uint32_t us = _HalCalculateI2CDelayUs(i2c.frq);
    bHalGpioConfig(i2c.sda.port, i2c.sda.pin, B_HAL_GPIO_INPUT, B_HAL_GPIO_NOPULL);
    bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
    bHalDelayUs(us);
    for (i = 0; i < 8; i++)
    {
        bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 1);
        tmp <<= 1;
        if (bHalGpioReadPin(i2c.sda.port, i2c.sda.pin))
        {
            tmp++;
        }
        bHalDelayUs(us);
        bHalGpioWritePin(i2c.clk.port, i2c.clk.pin, 0);
        bHalDelayUs(us);
    }
    bHalGpioConfig(i2c.sda.port, i2c.sda.pin, B_HAL_GPIO_OUTPUT, B_HAL_GPIO_NOPULL);
    return tmp;
}

static int _HalI2CIOWriteData(bHalI2CIO_t i2c, uint8_t dev, uint8_t dat)
{
    _HalI2CIOStart(i2c);
    _HalI2CIOWriteByte(i2c, dev);
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return -1;
    }
    _HalI2CIOWriteByte(i2c, dat);
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return -1;
    }
    _HalI2CIOStop(i2c);
    return 0;
}

static uint8_t _HalI2CIOReadData(bHalI2CIO_t i2c, uint8_t dev)
{
    uint8_t tmp;
    _HalI2CIOStart(i2c);
    _HalI2CIOWriteByte(i2c, dev | 0x1);
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return 0;
    }
    tmp = _HalI2CIOReadByte(i2c);
    _HalI2CIOStop(i2c);
    return tmp;
}

static int _HalI2CIOReadBuff(bHalI2CIO_t i2c, uint8_t dev, uint16_t addr, uint8_t addr_size,
                             uint8_t *pdat, uint8_t len)
{
    _HalI2CIOStart(i2c);
    _HalI2CIOWriteByte(i2c, dev);
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return -1;
    }

    if (addr_size > 1)
    {
        _HalI2CIOWriteByte(i2c, (uint8_t)((addr & 0xff00) >> 8));
        if (_HalI2CIOACK(i2c) < 0)
        {
            _HalI2CIOStop(i2c);
            return -1;
        }
    }
    _HalI2CIOWriteByte(i2c, (uint8_t)(addr & 0x00ff));
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return -1;
    }

    _HalI2CIOStart(i2c);
    _HalI2CIOWriteByte(i2c, dev | 0x1);
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return -1;
    }
    while (len-- > 1)
    {
        *pdat++ = _HalI2CIOReadByte(i2c);
        _HalI2CIOmACK(i2c);
    }
    *pdat++ = _HalI2CIOReadByte(i2c);

    _HalI2CIOStop(i2c);
    return 0;
}

static int _HalI2CIOWriteBuff(bHalI2CIO_t i2c, uint8_t dev, uint16_t addr, uint8_t addr_size,
                              const uint8_t *pdat, uint8_t len)
{
    uint32_t i = 0;
    _HalI2CIOStart(i2c);
    _HalI2CIOWriteByte(i2c, dev);
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return -1;
    }

    if (addr_size > 1)
    {
        _HalI2CIOWriteByte(i2c, (uint8_t)((addr & 0xff00) >> 8));
        if (_HalI2CIOACK(i2c) < 0)
        {
            _HalI2CIOStop(i2c);
            return -1;
        }
    }
    _HalI2CIOWriteByte(i2c, (uint8_t)(addr & 0x00ff));
    if (_HalI2CIOACK(i2c) < 0)
    {
        _HalI2CIOStop(i2c);
        return -1;
    }

    for (i = 0; i < len; i++)
    {
        _HalI2CIOWriteByte(i2c, pdat[i]);
        if (_HalI2CIOACK(i2c) < 0)
        {
            _HalI2CIOStop(i2c);
            return -1;
        }
    }
    _HalI2CIOStop(i2c);
    return 0;
}

/**
 * \brief        有些iic器件以主机iic的clk为时钟进行delay操作,该函数提供对应的空操作时钟
 * \param i2c
 * \param cnt
 * \return int
 */
static int _HalI2CIOClockPeriod(bHalI2CIO_t i2c, uint16_t cnt)
{
    uint16_t i = 0;
    _HalI2CIOStart(i2c);
    for (i = 0; i < cnt; i++)
    {
        _HalI2CIOWriteByte(i2c, 0xFF);
    }
    _HalI2CIOStop(i2c);

    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup I2C_Exported_Functions
 * \{
 */

__WEAKDEF uint8_t bMcuI2CReadByte(const bHalI2CIf_t *i2c_if)
{
    return 0;
}

__WEAKDEF int bMcuI2CWriteByte(const bHalI2CIf_t *i2c_if, uint8_t dat)
{
    return -1;
}

__WEAKDEF int bMcuI2CMemWrite(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                              const uint8_t *pbuf, uint16_t len)
{
    return -1;
}

__WEAKDEF int bMcuI2CMemRead(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                             uint8_t *pbuf, uint16_t len)
{
    return -1;
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

uint8_t bHalI2CReadByte(const bHalI2CIf_t *i2c_if)
{
    uint8_t     tmp = 0;
    bHalI2CIO_t simulating_iic;
    if (IS_NULL(i2c_if))
    {
        return 0;
    }
    if (i2c_if->is_simulation == 1)
    {
        simulating_iic.clk = i2c_if->_if.simulating_i2c.clk;
        simulating_iic.sda = i2c_if->_if.simulating_i2c.sda;
        simulating_iic.frq = i2c_if->_if.simulating_i2c.frq;
        tmp                = _HalI2CIOReadData(simulating_iic, i2c_if->dev_addr);
    }
    else
    {
        tmp = bMcuI2CReadByte(i2c_if);
    }
    return tmp;
}

int bHalI2CWriteByte(const bHalI2CIf_t *i2c_if, uint8_t dat)
{
    int         retval = 0;
    bHalI2CIO_t simulating_iic;
    if (IS_NULL(i2c_if))
    {
        return -1;
    }
    if (i2c_if->is_simulation == 1)
    {
        simulating_iic.clk = i2c_if->_if.simulating_i2c.clk;
        simulating_iic.sda = i2c_if->_if.simulating_i2c.sda;
        simulating_iic.frq = i2c_if->_if.simulating_i2c.frq;
        _HalI2CIOWriteData(simulating_iic, i2c_if->dev_addr, dat);
    }
    else
    {
        retval = bMcuI2CWriteByte(i2c_if, dat);
    }
    return retval;
}

int bHalI2CMemWrite(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                    const uint8_t *pbuf, uint16_t len)
{
    int         retval = 0;
    bHalI2CIO_t simulating_iic;
    if (IS_NULL(i2c_if))
    {
        return -1;
    }
    if (i2c_if->is_simulation == 1)
    {
        simulating_iic.clk = i2c_if->_if.simulating_i2c.clk;
        simulating_iic.sda = i2c_if->_if.simulating_i2c.sda;
        simulating_iic.frq = i2c_if->_if.simulating_i2c.frq;
        retval = _HalI2CIOWriteBuff(simulating_iic, i2c_if->dev_addr, mem_addr, mem_addr_size, pbuf,
                                    len);
    }
    else
    {
        retval = bMcuI2CMemWrite(i2c_if, mem_addr, mem_addr_size, pbuf, len);
    }
    return retval;
}

int bHalI2CMemRead(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                   uint8_t *pbuf, uint16_t len)
{
    int         retval = 0;
    bHalI2CIO_t simulating_iic;
    if (IS_NULL(i2c_if))
    {
        return -1;
    }
    if (i2c_if->is_simulation == 1)
    {
        simulating_iic.clk = i2c_if->_if.simulating_i2c.clk;
        simulating_iic.sda = i2c_if->_if.simulating_i2c.sda;
        simulating_iic.frq = i2c_if->_if.simulating_i2c.frq;
        retval = _HalI2CIOReadBuff(simulating_iic, i2c_if->dev_addr, mem_addr, mem_addr_size, pbuf, len);
    }
    else
    {
        retval = bMcuI2CMemRead(i2c_if, mem_addr, mem_addr_size, pbuf, len);
    }
    return retval;
}

int bHalI2CClockPeriod(const bHalI2CIf_t *i2c_if, uint16_t cnt)
{
    int         retval = 0;
    bHalI2CIO_t simulating_iic;
    if ((IS_NULL(i2c_if)) || (cnt == 0))
    {
        return -1;
    }
    if (i2c_if->is_simulation == 1)
    {
        simulating_iic.clk = i2c_if->_if.simulating_i2c.clk;
        simulating_iic.sda = i2c_if->_if.simulating_i2c.sda;
        simulating_iic.frq = i2c_if->_if.simulating_i2c.frq;
        retval             = _HalI2CIOClockPeriod(simulating_iic, cnt);
    }
    else
    {
        return -1;
    }
    return retval;
}
/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
