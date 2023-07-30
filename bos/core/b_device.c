/**
 *!
 * \file        b_device.c
 * \version     v0.0.1
 * \date        2019/06/05
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
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
#include "core/inc/b_device.h"
/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup CORE
 * \{
 */

/**
 * \addtogroup DEVICE
 * \{
 */

/**
 * \defgroup DEVICE_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup DEVICE_Private_Defines
 * \{
 */
#define B_DEVICE_NORMAL 0
#define B_DEVICE_ERROR 1
/**
 * \}
 */

/**
 * \defgroup DEVICE_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup DEVICE_Private_Variables
 * \{
 */
static bDriverNumber_t bDriverNumberTable[B_REG_DEV_NUMBER] = {
#define B_DEVICE_REG(dev, driver, desc) driver,
#include "b_device_list.h"
};

static bDriverInterface_t bDriverInterfaceTable[B_REG_DEV_NUMBER];

/**
 * \}
 */

/**
 * \defgroup DEVICE_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup DEVICE_Private_Functions
 * \{
 */

/**
 * \}
 */

/**
 * \addtogroup DEVICE_Exported_Functions
 * \{
 */

/**
 * \brief Device Initialize
 */
int bDeviceInit()
{
    uint8_t i = 0;
    for (i = 0; i < B_REG_DEV_NUMBER; i++)
    {
        bDriverInterfaceTable[i].ctl   = NULL;
        bDriverInterfaceTable[i].read  = NULL;
        bDriverInterfaceTable[i].write = NULL;
        bDriverInterfaceTable[i].init  = NULL;
    }
    return 0;
}

int bDeviceReinit(uint8_t dev_no)
{
    int retval = -1;
    if (dev_no >= B_REG_DEV_NUMBER)
    {
        return -1;
    }
    if (bDriverInterfaceTable[dev_no].init == NULL)
    {
        return B_DEVICE_FUNC_NULL;
    }

    if ((retval = bDriverInterfaceTable[dev_no].init(&bDriverInterfaceTable[dev_no])) == 0)
    {
        bDriverInterfaceTable[dev_no].status = B_DEVICE_NORMAL;
    }
    else
    {
        bDriverInterfaceTable[dev_no].status = B_DEVICE_ERROR;
    }

    return retval;
}

int bDeviceLoadDriver(bDriverNumber_t number, bDriverInit_t init)
{
    uint8_t i = 0, j = 0;
    if (number >= B_REG_DEV_NUMBER || init == NULL)
    {
        return -1;
    }
    for (i = 0; i < B_REG_DEV_NUMBER; i++)
    {
        if (bDriverNumberTable[i] == number)
        {
            if (bDriverInterfaceTable[i].init != NULL)
            {
                j += 1;
                continue;
            }
            bDriverInterfaceTable[i].drv_no = j;
            bDriverInterfaceTable[i].init   = init;
            if (init(&bDriverInterfaceTable[i]) == 0)
            {
                bDriverInterfaceTable[i].status = B_DEVICE_NORMAL;
            }
            else
            {
                bDriverInterfaceTable[i].status = B_DEVICE_ERROR;
            }
            break;
        }
    }
    return 0;
}

int bDeviceRead(uint8_t dev_no, uint8_t *pdat, uint16_t len)
{
    int         retval = B_DEVICE_FUNC_NULL;
    bOptParam_t param;
    if (dev_no >= B_REG_DEV_NUMBER || pdat == NULL)
    {
        return -1;
    }
    param.offset = bDriverInterfaceTable[dev_no].offset;
    param.pdrv   = &bDriverInterfaceTable[dev_no];
    param.pbuf   = pdat;
    param.len    = len;
    if (bDriverInterfaceTable[dev_no].status == B_DEVICE_NORMAL)
    {
        if (bDriverInterfaceTable[dev_no].read != NULL)
        {
            retval = bDriverInterfaceTable[dev_no].read(&param);
        }
    }
    else if (bDriverInterfaceTable[dev_no].status == B_DEVICE_ERROR)
    {
        retval = B_DEVICE_STAT_ERR;
    }

    return retval;
}

int bDeviceWrite(uint8_t dev_no, uint8_t *pdat, uint16_t len)
{
    int         retval = B_DEVICE_FUNC_NULL;
    bOptParam_t param;
    if (dev_no >= B_REG_DEV_NUMBER || pdat == NULL)
    {
        return -1;
    }
    param.offset = bDriverInterfaceTable[dev_no].offset;
    param.pdrv   = &bDriverInterfaceTable[dev_no];
    param.pbuf   = pdat;
    param.len    = len;
    if (bDriverInterfaceTable[dev_no].status == B_DEVICE_NORMAL)
    {
        if (bDriverInterfaceTable[dev_no].write != NULL)
        {
            retval = bDriverInterfaceTable[dev_no].write(&param);
        }
    }
    else if (bDriverInterfaceTable[dev_no].status == B_DEVICE_ERROR)
    {
        retval = B_DEVICE_STAT_ERR;
    }
    return retval;
}

int bDeviceCtl(uint8_t dev_no, uint8_t cmd, void *param)
{
    int         retval = B_DEVICE_FUNC_NULL;
    bCtlParam_t ctl_param;
    if (dev_no >= B_REG_DEV_NUMBER)
    {
        return -1;
    }
    ctl_param.pdrv  = &bDriverInterfaceTable[dev_no];
    ctl_param.cmd   = cmd;
    ctl_param.param = param;
    if (bDriverInterfaceTable[dev_no].status == B_DEVICE_NORMAL)
    {
        if (bDriverInterfaceTable[dev_no].ctl != NULL)
        {
            retval = bDriverInterfaceTable[dev_no].ctl(&ctl_param);
        }
    }
    else if (bDriverInterfaceTable[dev_no].status == B_DEVICE_ERROR)
    {
        retval = B_DEVICE_STAT_ERR;
    }
    return retval;
}

int bDeviceISNormal(uint8_t dev_no)
{
    if (dev_no >= B_REG_DEV_NUMBER)
    {
        return -1;
    }
    return (bDriverInterfaceTable[dev_no].status != B_DEVICE_ERROR);
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
