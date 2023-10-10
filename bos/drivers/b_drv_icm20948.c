/**
 *!
 * \file        b_drv_ICM20948.c
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_ICM20948.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup ICM20948
 * \{
 */

/**
 * \defgroup ICM20948_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_Defines
 * \{
 */
#define DRIVER_NAME ICM20948
/**
 * \}
 */

/** Device Identification **/
#define ICM20948_ID 0xEAU

/**
 * \defgroup ICM20948_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bICM20948_HalIf_t, DRIVER_NAME);

static bICM20948Private_t bICM20948RunInfo[bDRIVER_HALIF_NUM(bICM20948_HalIf_t, DRIVER_NAME)];

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_Functions
 * \{
 */

static int _bICM20948Write(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    uint32_t i = 0, l_c = 0;
    bDRIVER_GET_HALIF(_if, bICM20948_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bICM20948Private_t, pdrv);

    l_c = _priv->page_size - off % (_priv->page_size);
    if (len <= l_c)
    {
        bHalI2CMemWrite(_if, off, 1 + (_priv->capacity > 256), pbuf, len);
    }
    else
    {
        bHalI2CMemWrite(_if, off, 1 + (_priv->capacity > 256), pbuf, l_c);
        bHalDelayMs(5);
        off += l_c;
        pbuf += l_c;
        len -= l_c;
        for (i = 0; i < len / (_priv->page_size); i++)
        {
            bHalI2CMemWrite(_if, off, 1 + (_priv->capacity > 256), pbuf, _priv->page_size);
            bHalDelayMs(5);
            off += _priv->page_size;
            pbuf += _priv->page_size;
        }
        if ((len % _priv->page_size) > 0)
        {
            bHalI2CMemWrite(_if, off, 1 + (_priv->capacity > 256), pbuf, (len % _priv->page_size));
            bHalDelayMs(5);
        }
    }
    return len;
}

static int _bICM20948Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bICM20948_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bICM20948Private_t, pdrv);
    bHalI2CMemRead(_if, off, 1 + (_priv->capacity > 256), pbuf, len);
    return len;
}

//----------------------------------------------------------------
static int _bICM20948ReadRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data, uint16_t len)
{

    return 0;
}

static uint8_t _bICM20948GetID(bDriverInterface_t *pdrv)
{
    uint8_t id = 0;

    // F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);
    // xg_id = F_IIC_ReadByte(I2C_ADD_ICM20948, REG_ADD_WIA);

    // _bICM20948ReadRegs(pdrv, ICM20948_WHO_AM_I, &id, 1);
    // b_log("ICM20948 id:%x\n", id);
    return id;
}
/**
 * \}
 */

/**
 * \addtogroup ICM20948_Exported_Functions
 * \{
 */
int bICM20948_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bICM20948_Init);
    pdrv->read  = _bICM20948Read;
    pdrv->_private._p                     = &bICM20948RunInfo[pdrv->drv_no];
    memset(pdrv->_private._p, 0, sizeof(bICM20948Private_t));
    if ((_bICM20948GetID(pdrv)) != ICM20948_ID)
    {
            return -1;
    }
    
    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_ICM20948, bICM20948_Init);

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
