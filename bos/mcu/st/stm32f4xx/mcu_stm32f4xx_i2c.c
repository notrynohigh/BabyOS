#include "b_config.h"
#include "hal/inc/b_hal_i2c.h"

int bMcuI2CReadByte(const bHalI2CIf_t *i2c_if, uint8_t *pbuf, uint16_t len)
{
    return -1;
}

int bMcuI2CWriteByte(const bHalI2CIf_t *i2c_if, uint8_t *pbuf, uint16_t len)
{
    return -1;
}

int bMcuI2CMemWrite(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                    const uint8_t *pbuf, uint16_t len)
{
    return -1;
}

int bMcuI2CMemRead(const bHalI2CIf_t *i2c_if, uint16_t mem_addr, uint8_t mem_addr_size,
                   uint8_t *pbuf, uint16_t len)
{
    return -1;
}
