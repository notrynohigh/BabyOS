/**
 *!
 * \file        b_drv_smp3011.c
 * \version     v0.0.1
 * \date        2025/11/18
 * \author      Jerry(3463866261@qq.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 Jerry
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
#include "drivers/inc/b_drv_smp3011.h"

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup SMP3011
 * \{
 */

/**
 * \defgroup SMP3011_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SMP3011_Private_Defines
 * \{
 */
#define DRIVER_NAME SMP3011
/**
 * \}
 */

/**
 * \defgroup SMP3011_Private_Macros
 * \{
 */
// Define the upper and lower limits of the calibration pressure
#define PMIN 0.0         // Full range pressure for example 20Kpa
#define PMAX 35000.0     // Zero Point Pressure Value, for example 120Kpa
#define DMIN 2516582.0   // AD value corresponding to pressure zero, for example 15%AD
#define DMAX 14260633.0  // AD Value Corresponding to Full Pressure Range, for example 85%AD
#define TMAX 150         // 温度
#define TMIN -40
/**
 * \}
 */

/**
 * \defgroup SMP3011_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bSMP3011_HalIf_t, DRIVER_NAME);

static bSMP3011Private_t bSMP3011RunInfo[bDRIVER_HALIF_NUM(bSMP3011_HalIf_t, DRIVER_NAME)];

/**
 * \}
 */

/**
 * \defgroup SMP3011_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SMP3011_Private_Functions
 * \{
 */
int _bSMP3011IsBusy(const bHalI2CIf_t *i2c_if)
{
    uint8_t status = 0;
    if (bHalI2CReadByte(i2c_if, &status, 1) != 0)
    {
        return -1;
    }
    status = (status >> 5) & 0x01;
    return status;
}

/*****************************************driver interface***************************************/

static int _bSMP3011Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bSMP3011_HalIf_t, pdrv);
    uint8_t calibration_cmd = 0xAC;  // 校准命令
    uint8_t smp3011_timeout = 0;

    if (bHalI2CWriteByte(_if, &calibration_cmd, 1) != 0)  // 发送校准命令
    {
        return -1;
    }
    while (1)
    {
        int busy = _bSMP3011IsBusy(_if);
        if (busy < 0)
        {
            return -1;
        }
        if (busy)
        {
            smp3011_timeout++;
            bHalDelayMs(1);
            if (smp3011_timeout == 256)
            {
                return 0;  // 超时,本次读取数据失败
            }
        }
        else
        {
            break;  // 数据已准备好
        }
    }

    if (bHalI2CReadByte(_if, pbuf, len) != 0)  // 读取6字节原始数据
    {
        return -1;
    }

    return len;
}

static int _bSMP3011Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bDRIVER_GET_HALIF(_if, bSMP3011_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bSMP3011Private_t, pdrv);

    uint16_t           temp_raw     = 0;
    uint32_t           pressure_raw = 0;
    double             pressure = 0.0, temp = 0.0;
    uint8_t            smp3011_buffer[6] = {0};
    bSMP3011Private_t *smp3011_param     = (bSMP3011Private_t *)param;
    uint8_t            otp_reg_adrr      = 0x14;
    uint16_t           otp_value         = 0;

    _bSMP3011Read(pdrv, 0, smp3011_buffer, 6);

    switch (cmd)
    {
        case SMP3011_READ_P_CMD:
        {
            /*返回的压力值根据校准范围转换为实际值*/
            pressure_raw = ((uint32_t)smp3011_buffer[1] << 16) |
                           ((uint16_t)smp3011_buffer[2] << 8) | smp3011_buffer[3];

            /*Pressure = (Pmax - Pmin)/(Dmax - Dmin)*(Dtest - Dmin) + Pmin
              Pressure:实际压力值;  Dtest:传感器的数字输出值；
              Pmax:传感器满量程压力值；Pmin:传感器零点压力值;
              Dmax:传感器满量程时对应的数字输出值；Dmin零点时对应的数字输出值；
            */
            pressure = (_priv->pressure_rang_up - _priv->pressure_rang_low) / (DMAX - DMIN) *
                           (pressure_raw - DMIN) +
                       _priv->pressure_rang_low;
            smp3011_param->pressure = pressure;
        }
        break;
        case SMP3011_READ_T_CMD:
        {
            /*返回的温度值根据校准范围转换为实际值*/
            temp_raw = ((uint16_t)smp3011_buffer[4] << 8) | (smp3011_buffer[5] << 0);

            /*Temperature = (Tmax - Tmin)*temp + Tmin
              Temperature:校准后的实际温度值
              Tmax:校准范围最大值
              Tmin:校准范围最小值
            */
            temp = (double)temp_raw / 65536.0;
            temp = (_priv->temperature_rang_up - _priv->temperature_rang_low) * temp +
                   _priv->temperature_rang_low;

            smp3011_param->temperature = temp;
        }
        break;
        case SMP3011_READ_P_T_CMD:
        {
            /*返回的压力和温度值根据校准范围转换为实际值*/
            pressure_raw = ((uint32_t)smp3011_buffer[1] << 16) |
                           ((uint16_t)smp3011_buffer[2] << 8) | smp3011_buffer[3];
            pressure = (_priv->pressure_rang_up - _priv->pressure_rang_low) / (DMAX - DMIN) *
                           (pressure_raw - DMIN) +
                       _priv->pressure_rang_low;
            smp3011_param->pressure = pressure;

            temp_raw = ((uint16_t)smp3011_buffer[4] << 8) | (smp3011_buffer[5] << 0);
            temp     = (double)temp_raw / 65536.0;
            temp     = (_priv->temperature_rang_up - _priv->temperature_rang_low) * temp +
                   _priv->temperature_rang_low;
            smp3011_param->temperature = temp;
        }
        break;
        case SMP3011_SET_P_UPRANG_CMD:
        {
            _priv->pressure_rang_up = smp3011_param->pressure_rang_up;  // 设置压力量程上限
        }
        break;
        case SMP3011_SET_P_LOWRANG_CMD:
        {
            _priv->pressure_rang_low = smp3011_param->pressure_rang_low;  // 设置压力量程下限
        }
        break;
        case SMP3011_READ_OTP_CMD:
        {
            if (bHalI2CWriteByte(_if, &otp_reg_adrr, 1) != 0)  // 读取OTP寄存器数据请求
            {
                return -1;
            }
            if (bHalI2CReadByte(_if, (uint8_t *)&otp_value, 2) != 0)  // 读取3字节原始数据
            {
                return -1;
            }
            b_log("otp_value:0x%x\r\n", otp_value);
        }
        default:
            break;
    }

    return 0;
}

static int _bSMP3011Open(bDriverInterface_t *pdrv)
{
    return 0;
}

static int _bSMP3011Close(bDriverInterface_t *pdrv)
{
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup SMP3011_Exported_Functions
 * \{
 */
int bSMP3011_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bSMP3011_Init);
    pdrv->read        = _bSMP3011Read;
    pdrv->write       = NULL;
    pdrv->ctl         = _bSMP3011Ctl;
    pdrv->open        = _bSMP3011Open;
    pdrv->close       = _bSMP3011Close;
    pdrv->_private._p = &bSMP3011RunInfo[pdrv->drv_no];
    memset(pdrv->_private._p, 0, sizeof(bSMP3011Private_t));
    bSMP3011RunInfo[pdrv->drv_no].pressure_rang_up     = 35.0;
    bSMP3011RunInfo[pdrv->drv_no].pressure_rang_low    = 0.0;
    bSMP3011RunInfo[pdrv->drv_no].temperature_rang_up  = TMAX;
    bSMP3011RunInfo[pdrv->drv_no].temperature_rang_low = TMIN;

    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_SMP3011, bSMP3011_Init);

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */
