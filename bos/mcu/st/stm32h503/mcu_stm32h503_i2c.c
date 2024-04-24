#include "b_config.h"
#include "hal/inc/b_hal_i2c.h"

#ifdef USE_HAL_DRIVER

#include "stm32h5xx_hal.h"

#ifdef HAL_I2C_MODULE_ENABLED

#include "stm32h5xx_hal_i2c.h"

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;

int bMcuI2CReadByte(const bHalI2CIf_t *i2c_if, uint8_t *pbuf, uint16_t len)
{
    if (i2c_if->_if.i2c == B_HAL_I2C_1)
    {
        return HAL_I2C_Master_Receive(&hi2c1, i2c_if->dev_addr, pbuf, len, 0xf * len);
    }
    else if (i2c_if->_if.i2c == B_HAL_I2C_2)
    {
        return HAL_I2C_Master_Receive(&hi2c2, i2c_if->dev_addr, pbuf, len, 0xf * len);
    }
    return -1;
}

int bMcuI2CWriteByte(const bHalI2CIf_t *i2c_if, uint8_t *pbuf, uint16_t len)
{
    if (i2c_if->_if.i2c == B_HAL_I2C_1)
    {
        return HAL_I2C_Master_Transmit(&hi2c1, i2c_if->dev_addr, pbuf, len, 0xf * len);
    }
    else if (i2c_if->_if.i2c == B_HAL_I2C_2)
    {
        return HAL_I2C_Master_Transmit(&hi2c2, i2c_if->dev_addr, pbuf, len, 0xf * len);
    }
    return -1;
}

int bMcuI2CMemWrite(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                    const uint8_t *pbuf, uint16_t len)
{
    if (i2c_if->_if.i2c == B_HAL_I2C_1)
    {
        return HAL_I2C_Mem_Write(&hi2c1, i2c_if->dev_addr, mem_addr, mem_addr_size, (uint8_t *)pbuf,
                                 len, 0xf * len);
    }
    else if (i2c_if->_if.i2c == B_HAL_I2C_2)
    {
        return HAL_I2C_Mem_Write(&hi2c2, i2c_if->dev_addr, mem_addr, mem_addr_size, (uint8_t *)pbuf,
                                 len, 0xf * len);
    }
    return -1;
}

int bMcuI2CMemRead(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                   uint8_t *pbuf, uint16_t len)
{
    if (i2c_if->_if.i2c == B_HAL_I2C_1)
    {
        return HAL_I2C_Mem_Read(&hi2c1, i2c_if->dev_addr, mem_addr, mem_addr_size, pbuf, len,
                                0xf * len);
    }
    else if (i2c_if->_if.i2c == B_HAL_I2C_2)
    {
        return HAL_I2C_Mem_Read(&hi2c2, i2c_if->dev_addr, mem_addr, mem_addr_size, pbuf, len,
                                0xf * len);
    }
    return -1;
}

#endif

#endif
