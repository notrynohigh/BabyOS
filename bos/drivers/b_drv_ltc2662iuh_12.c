/**
 * \file        b_drv_ltc2662iuh_12.c
 * \version     v0.0.1
 * \date        2024-02-19
 * \author      miniminiminini (405553848@qq.com)
 * \brief       LTC2662IUH_12驱动
 *
 * Copyright (c) 2024 by miniminiminini. All Rights Reserved.
 */

/* Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_ltc2662iuh_12.h"

/**
 * \defgroup LTC2662IUH_12_Private_FunctionPrototypes
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Exported_Variables
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Defines
 * \{
 */
#define DRIVER_NAME LTC2662IUH_12

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_TypesDefinitions
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Macros
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bLTC2662IUH_12_HalIf_t, DRIVER_NAME);

static bLTC2662IUH_12Private_t
    bLTC2662IUH_12RunInfo[bDRIVER_HALIF_NUM(bLTC2662IUH_12_HalIf_t, DRIVER_NAME)];

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Functions
 * \{
 */

static int _bLTC2662_Send(bDriverInterface_t *pdrv, uint8_t *pbuf, uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);

    bHalSpiSend(_if, pbuf, len);

    return 0;
}

/**
 * @brief 通过对应dac通道的expect_current来更新span与span_value
 *
 * @param pdrv
 * @param dac_x
 * @return LTC2662_SPAN_t
 */
static int _bLTC2662_DACX_Updata_Span(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x)
{
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    float current = _priv->dac_attribute[dac_x].expect_current;

    if (dac_x >= LTC_DAC_MAX)
    {
        return -1;
    }

    if (current < LTC_SPAN_3_125)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_3_125mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_3_125;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_3_125 / 4095;
    }
    else if (current < LTC_SPAN_6_25)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_6_25mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_6_25;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_6_25 / 4095;
    }
    else if (current < LTC_SPAN_12_5)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_12_5mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_12_5;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_12_5 / 4095;
    }
    else if (current < LTC_SPAN_25)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_25mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_25;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_25 / 4095;
    }
    else if (current < LTC_SPAN_50)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_50mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_50;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_50 / 4095;
    }
    else if (current < LTC_SPAN_100)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_100mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_100;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_100 / 4095;
    }
    else if (current < LTC_SPAN_200)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_200mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_200;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_200 / 4095;
    }
    else if (current < LTC_SPAN_300)
    {
        _priv->dac_attribute[dac_x].span = LTC_SPAN_300mA;
        _priv->dac_attribute[dac_x].span_value = LTC_SPAN_300;
        _priv->dac_attribute[dac_x].span_div = LTC_SPAN_300 / 4095;
    }
    else
    {
        return -2;
    }

    return 0;
}

/**
 * @brief 当期待电流变化时更新某一个通道的DAC属性值
 *
 * @param pdrv
 * @param dac_x
 * @return int
 */
static int _bLTC2662_DACX_Attribute_Updata(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x)
{
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);

    if (dac_x >= LTC_DAC_MAX)
    {
        return -1;
    }

    if (_bLTC2662_DACX_Updata_Span(pdrv, dac_x) < 0)
    {
        return -2;
    }

    _priv->dac_attribute[dac_x].set_value = _priv->dac_attribute[dac_x].expect_current / _priv->dac_attribute[dac_x].span_div;
    _priv->dac_attribute[dac_x].set_value = _priv->dac_attribute[dac_x].set_value > 4095 ? 4095 : _priv->dac_attribute[dac_x].set_value;

    _priv->dac_attribute[dac_x].real_current = _priv->dac_attribute[dac_x].set_value * _priv->dac_attribute[dac_x].span_div;

    _priv->dac_attribute[dac_x].err_current = _priv->dac_attribute[dac_x].expect_current - _priv->dac_attribute[dac_x].real_current;

    _priv->dac_attribute[dac_x].err_percentage_current = _priv->dac_attribute[dac_x].err_current / _priv->dac_attribute[dac_x].expect_current;

    return 0;
}

/**
 * \brief     写入范围到n
 * \param pdrv
 * \param dac_x
 * \param range
 * \return int
 */
static int _bLTC2662_WriteSpanToN(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x)
{
    uint8_t send_buf[3] = {0, 0, 0};
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    if (dac_x >= LTC_DAC_MAX)
    {
        return -1;
    }

    // 0 	1 	1 	0 	Write Span to n
    if (dac_x == LTC_DAC_0)
    {
        send_buf[0] = 0x60 | 0x00;
    }
    else if (dac_x == LTC_DAC_1)
    {
        send_buf[0] = 0x60 | 0x01;
    }
    else if (dac_x == LTC_DAC_2)
    {
        send_buf[0] = 0x60 | 0x02;
    }
    else if (dac_x == LTC_DAC_3)
    {
        send_buf[0] = 0x60 | 0x03;
    }
    else if (dac_x == LTC_DAC_4)
    {
        send_buf[0] = 0x60 | 0x04;
    }

    if (_priv->dac_attribute[dac_x].span == LTC_SPAN_3_125mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x01;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_6_25mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x02;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_12_5mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x03;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_25mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x04;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_50mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x05;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_100mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x06;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_200mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x07;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_300mA)
    {
        send_buf[1] = 0x00;
        send_buf[2] = 0x00 & 0x0f;
    }

    // spi发送
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 0);
    _bLTC2662_Send(pdrv, send_buf, 3);
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 1);

    return 0;
}

/**
 * \brief   写入代码至 n，更新 n（上电）
 * \param pdrv
 * \param dac_x
 * \param range
 * \return int
 */
static int _bLTC2662_WriteCodeToNUpdateN(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x)
{
    uint8_t send_buf[3] = {0, 0, 0};
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    if (dac_x >= LTC_DAC_MAX)
    {
        return -1;
    }

    // 0 	0 	1 	1
    if (dac_x == LTC_DAC_0)
    {
        send_buf[0] = 0x30 | 0x00;
    }
    else if (dac_x == LTC_DAC_1)
    {
        send_buf[0] = 0x30 | 0x01;
    }
    else if (dac_x == LTC_DAC_2)
    {
        send_buf[0] = 0x30 | 0x02;
    }
    else if (dac_x == LTC_DAC_3)
    {
        send_buf[0] = 0x30 | 0x03;
    }
    else if (dac_x == LTC_DAC_4)
    {
        send_buf[0] = 0x30 | 0x04;
    }

    // 将set_value由16bit转换为12bit,后四个无关位,并且进行MSB-TO-LSB
    uint16_t tempdata = _priv->dac_attribute->set_value << 4;
    send_buf[1]       = (tempdata >> 8) & 0xff;
    send_buf[2]       = (tempdata) & 0xff;

    // spi发送
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 0);
    _bLTC2662_Send(pdrv, send_buf, 3);
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 1);

    return 0;
}

static int _bLTC2662_DACX_Exec(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x)
{
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    if (dac_x >= LTC_DAC_MAX)
    {
        return -1;
    }
    if (_bLTC2662_WriteSpanToN(pdrv, dac_x) < 0)
    {
        return -2;
    }
    if (_bLTC2662_WriteCodeToNUpdateN(pdrv, dac_x) < 0)
    {
        return -3;
    }

    _priv->dac_attribute[dac_x].status = 1;

    return 0;
}

static int _bLTC2662_DACX_Stop(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x)
{
    uint8_t send_buf[3] = {0, 0, 0};
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    if (dac_x >= LTC_DAC_MAX)
    {
        return -1;
    }

    // 0 	0 	1 	1
    if (dac_x == LTC_DAC_0)
    {
        send_buf[0] = 0x30 | 0x00;
    }
    else if (dac_x == LTC_DAC_1)
    {
        send_buf[0] = 0x30 | 0x01;
    }
    else if (dac_x == LTC_DAC_2)
    {
        send_buf[0] = 0x30 | 0x02;
    }
    else if (dac_x == LTC_DAC_3)
    {
        send_buf[0] = 0x30 | 0x03;
    }
    else if (dac_x == LTC_DAC_4)
    {
        send_buf[0] = 0x30 | 0x04;
    }

    send_buf[1] = 0;
    send_buf[2] = 0;

    // spi发送
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 0);
    _bLTC2662_Send(pdrv, send_buf, 3);
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 1);

    _priv->dac_attribute[dac_x].status = 0;

    return 0;
}

static int _bLTC2662IUH_12Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    bLTC2662_CONFIG_t *p = (bLTC2662_CONFIG_t *)param;

    switch (cmd)
    {
    case bCMD_LTC_SET_CURRENT:
        if (param == NULL)
        {
            return -1;
        }
        _priv->dac_attribute[p->dac_channel].expect_current = p->current;
        break;
    case bCMD_LTC_EXEC_DACX:
        if (param == NULL)
        {
            return -1;
        }
        if (p->dac_channel >= LTC_DAC_MAX)
        {
            return -2;
        }
        if (p->current != _priv->dac_attribute[p->dac_channel].expect_current)
        {
            _priv->dac_attribute[p->dac_channel].expect_current = p->current;
        }
        if (_bLTC2662_DACX_Attribute_Updata(pdrv, p->dac_channel) < 0)
        {
            return -3;
        }
        if (_bLTC2662_DACX_Exec(pdrv, p->dac_channel) < 0)
        {
            return -4;
        }
        break;
    case bCMD_LTC_STOP_DACX:
        if (param == NULL)
        {
            return -1;
        }
        if (p->dac_channel >= LTC_DAC_MAX)
        {
            return -2;
        }
        if (_bLTC2662_DACX_Stop(pdrv, p->dac_channel) < 0)
        {
            return -3;
        }
        break;
    default:
        break;
    }
    return 0;
}

/**
 * \brief 关断模式
 * \param pdrv
 * \return int
 */
static int _bLTC2662IUH_12Close(bDriverInterface_t *pdrv)
{
    for (uint8_t i = 0; i < LTC_DAC_MAX; i++)
    {
        _bLTC2662_DACX_Stop(pdrv, (LTC2662_DAC_t)i);
    }

    return 0;
}
/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Exported_Functions
 * \{
 */
int bLTC2662IUH_12_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bLTC2662IUH_12_Init);
    pdrv->ctl = _bLTC2662IUH_12Ctl;
    pdrv->close = _bLTC2662IUH_12Close;
    pdrv->_private._p = &bLTC2662IUH_12RunInfo[pdrv->drv_no];
    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_LTC2662IUH_12, bLTC2662IUH_12_Init);
/**
 * }
 */

/**
 * }
 */

/**
 * }
 */

/**
 * }
 */

/***** Copyright (c) 2024 miniminiminini *****END OF FILE*****/
