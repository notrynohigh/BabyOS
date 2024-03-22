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
#include <math.h>
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

#define DAC0 0X00
#define DAC1 0X01
#define DAC2 0X02
#define DAC3 0X03
#define DAC4 0X04

#define L2B_B2L_16b(n) ((((n) & 0xff) << 8) | (((n) & 0xff00) >> 8))

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

bLTC2662IUH_12Private_t
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
        b_log_e("Invalid DAC channel");
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
        b_log_e("Invalid current value");
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
        b_log_e("Invalid DAC channel");
        return -1;
    }

    if (_bLTC2662_DACX_Updata_Span(pdrv, dac_x) < 0)
    {
        b_log_e("Invalid current value");
        return -2;
    }

    _priv->dac_attribute[dac_x].set_value = round(_priv->dac_attribute[dac_x].expect_current / _priv->dac_attribute[dac_x].span_div);
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
    uint8_t    send_buf[3] = {0, 0, 0};
    U_LT_Write data;
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    if (dac_x >= LTC_DAC_MAX)
    {
        b_log_e("Invalid DAC channel");
        return -1;
    }

    // 0 	1 	1 	0 	Write Span to n
    if (dac_x == LTC_DAC_0)
    {
        send_buf[0] = WRITE_SPAN_TO_N | DAC0;
    }
    else if (dac_x == LTC_DAC_1)
    {
        send_buf[0] = WRITE_SPAN_TO_N | DAC1;
    }
    else if (dac_x == LTC_DAC_2)
    {
        send_buf[0] = WRITE_SPAN_TO_N | DAC2;
    }
    else if (dac_x == LTC_DAC_3)
    {
        send_buf[0] = WRITE_SPAN_TO_N | DAC3;
    }
    else if (dac_x == LTC_DAC_4)
    {
        send_buf[0] = WRITE_SPAN_TO_N | DAC4;
    }

    if (_priv->dac_attribute[dac_x].span == LTC_SPAN_3_125mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_3_125mA;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_6_25mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_6_25mA;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_12_5mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_12_5mA;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_25mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_25mA;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_50mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_50mA;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_100mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_100mA;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_200mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_200mA;
    }
    else if (_priv->dac_attribute[dac_x].span == LTC_SPAN_300mA)
    {
        data.LT_uint16 = LTC_SPAN_VALUE_300mA;
    }

    send_buf[1] = data.LT_byte[1];
    send_buf[2] = data.LT_byte[0];

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
    uint8_t    send_buf[3] = {0, 0, 0};
    U_LT_Write data;
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    if (dac_x >= LTC_DAC_MAX)
    {
        b_log_e("Invalid DAC channel");
        return -1;
    }

    // 0 	0 	1 	1
    if (dac_x == LTC_DAC_0)
    {
        send_buf[0] = WRITE_CODE_TO_N_UPDATE_N | DAC0;
    }
    else if (dac_x == LTC_DAC_1)
    {
        send_buf[0] = WRITE_CODE_TO_N_UPDATE_N | DAC1;
    }
    else if (dac_x == LTC_DAC_2)
    {
        send_buf[0] = WRITE_CODE_TO_N_UPDATE_N | DAC2;
    }
    else if (dac_x == LTC_DAC_3)
    {
        send_buf[0] = WRITE_CODE_TO_N_UPDATE_N | DAC3;
    }
    else if (dac_x == LTC_DAC_4)
    {
        send_buf[0] = WRITE_CODE_TO_N_UPDATE_N | DAC4;
    }

    // 将set_value由16bit转换为12bit,后四个无关位,并且进行MSB-TO-LSB
    data.LT_uint16 = _priv->dac_attribute[dac_x].set_value;

    // 将LT_byte按照大小端进行转换
    data.LT_uint16 = L2B_B2L_16b(data.LT_uint16);
    // 将其左移四位
    data.LT_uint16 = data.LT_uint16 << 4;

    send_buf[1] = data.LT_byte[0];
    send_buf[2] = data.LT_byte[1];

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
        b_log_e("Invalid DAC channel");
        return -1;
    }
    if (_bLTC2662_WriteSpanToN(pdrv, dac_x) < 0)
    {
        b_log_e("_bLTC2662_WriteSpanToN chx=%d err!!!\r\n", dac_x);
        return -2;
    }
    if (_bLTC2662_WriteCodeToNUpdateN(pdrv, dac_x) < 0)
    {
        b_log_e("_bLTC2662_WriteCodeToNUpdateN chx=%d err!!!\r\n", dac_x);
        return -3;
    }

    _priv->dac_attribute[dac_x].status = 1;

    // b_log_i("\r\n%d\t%10.3f\t%10.3f\t%10.3f\t%10.3f\t\r\n", dac_x,
    //         _priv->dac_attribute[dac_x].expect_current, _priv->dac_attribute[dac_x].real_current,
    //         _priv->dac_attribute[dac_x].err_current,
    //         _priv->dac_attribute[dac_x].err_percentage_current);

    return 0;
}

static int _bLTC2662_DACX_Stop(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x)
{
    uint8_t send_buf[3] = {0, 0, 0};
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    if (dac_x >= LTC_DAC_MAX)
    {
        b_log_e("Invalid DAC channel");
        return -1;
    }

    // 0 	1 	0 	0 Power Down N
    if (dac_x == LTC_DAC_0)
    {
        send_buf[0] = POWER_DOWN_N | DAC0;
    }
    else if (dac_x == LTC_DAC_1)
    {
        send_buf[0] = POWER_DOWN_N | DAC1;
    }
    else if (dac_x == LTC_DAC_2)
    {
        send_buf[0] = POWER_DOWN_N | DAC2;
    }
    else if (dac_x == LTC_DAC_3)
    {
        send_buf[0] = POWER_DOWN_N | DAC3;
    }
    else if (dac_x == LTC_DAC_4)
    {
        send_buf[0] = POWER_DOWN_N | DAC4;
    }

    send_buf[1] = 0;
    send_buf[2] = 0;

    // spi发送
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 0);
    _bLTC2662_Send(pdrv, send_buf, 3);
    bHalGpioWritePin(_if->cs.port, _if->cs.pin, 1);

    _priv->dac_attribute[dac_x].status = 0;
    _priv->dac_attribute[dac_x].expect_current = 0;

    // b_log_i("\r\n%d\t%d\t\r\n", dac_x, _priv->dac_attribute[dac_x].status);

    return 0;
}

static int _bLTC2662IUH_12Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bDRIVER_GET_PRIVATE(_priv, bLTC2662IUH_12Private_t, pdrv);
    bLTC2662_CONFIG_t *p = (bLTC2662_CONFIG_t *)param;
    bLTC2662_STATUS_t *q = (bLTC2662_STATUS_t *)param;

    switch (cmd)
    {
        case bCMD_LTC_GET_DACX_STATUS:
            if (param == NULL)
            {
                b_log_e("bCMD_LTC_GET_DACX_STATUS param err!!!\r\n");
                return -1;
            }
            q->status = _priv->dac_attribute[q->dac_channel].status;
            break;
        case bCMD_LTC_SET_CURRENT:
            if (param == NULL)
            {
                b_log_e("bCMD_LTC_SET_CURRENT param err!!!\r\n");
                return -1;
            }
            _priv->dac_attribute[p->dac_channel].expect_current = p->current;
            break;
        case bCMD_LTC_GET_CURRENT:
            if (param == NULL)
            {
                b_log_e("bCMD_LTC_GET_CURRENT param err!!!\r\n");
                return -1;
            }
            p->current = _priv->dac_attribute[p->dac_channel].real_current;
            break;
        case bCMD_LTC_EXEC_DACX:
            if (param == NULL)
            {
                b_log_e("bCMD_LTC_EXEC_DACX param err!!!\r\n");
                return -1;
            }
            if (p->dac_channel >= LTC_DAC_MAX)
            {
                b_log_e("bCMD_LTC_EXEC_DACX dac_channel err!!!\r\n");
                return -2;
            }
            if (p->current < 0)
            {
                b_log_e("bCMD_LTC_EXEC_DACX current err!!!\r\n");
                return -3;
            }
            if (p->current == _priv->dac_attribute[p->dac_channel].expect_current)
            {
                // b_log_i("bCMD_LTC_EXEC_DACX current is equal to expect_current, no need to
                // exec!!!\r\n");
                return 0;
            }
            else
            {
                _priv->dac_attribute[p->dac_channel].expect_current = p->current;
            }
            if (_bLTC2662_DACX_Attribute_Updata(pdrv, p->dac_channel) < 0)
            {
                b_log_e("_bLTC2662_DACX_Attribute_Updata chx=%d err!!!\r\n", p->dac_channel);
                return -4;
            }
            if (_bLTC2662_DACX_Exec(pdrv, p->dac_channel) < 0)
            {
                b_log_e("_bLTC2662_DACX_Exec chx=%d err!!!\r\n", p->dac_channel);
                return -5;
            }
            break;
        case bCMD_LTC_STOP_DACX:
            if (param == NULL)
            {
                b_log_e("bCMD_LTC_STOP_DACX param err!!!\r\n");
                return -1;
            }
            if (p->dac_channel >= LTC_DAC_MAX)
            {
                b_log_e("bCMD_LTC_STOP_DACX dac_channel err!!!\r\n");
                return -2;
            }
            if (_bLTC2662_DACX_Stop(pdrv, p->dac_channel) < 0)
            {
                b_log_e("_bLTC2662_DACX_Stop chx=%d err!!!\r\n", p->dac_channel);
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

    _bLTC2662IUH_12Close(pdrv);

    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_LTC2662IUH_12, bLTC2662IUH_12_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section 
#endif
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
