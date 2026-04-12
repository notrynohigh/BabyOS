/**
 *!
 * \file        b_drv_ads125x.c
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
/*Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_ads125x.h"


#include "utils/inc/b_util_log.h"
#include "hal/inc/b_hal.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup ADS125X
 * \{
 */

/**
 * \defgroup ADS125X_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ADS125X_Private_Defines
 * \{
 */
#define DRIVER_NAME ADS125X
/**
 * \}
 */

/**
 * \defgroup ADS125X_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ADS125X_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bADS125X_HalIf_t, DRIVER_NAME);

static bADS125XPrivate_t bADS125XRunInfo[bDRIVER_HALIF_NUM(bADS125X_HalIf_t, DRIVER_NAME)];

/**
 * \}
 */

/**
 * \defgroup ADS125X_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ADS125X_Private_Functions
 * \{
 */

static void _bADS125XSendByte(bDriverInterface_t *pdrv,uint8_t data)
{
   bDRIVER_GET_HALIF(_if, bADS125X_HalIf_t, pdrv);

   {
       uint32_t tick = bHalGetSysTick();
       while (bHalGpioReadPin(_if->drdy.port,_if->drdy.pin))
       {
           if (TICK_DIFF_BIT32(tick, bHalGetSysTick()) > MS2TICKS(1000))
           {
               return;
           }
       }
   }
   bHalSpiSend(&_if->_spi,&data,1);
}

static uint8_t _bADS125XReceiveByte(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bADS125X_HalIf_t, pdrv);
    uint8_t Result = 0;

    if (bHalSpiReceive(&_if->_spi, &Result, 1) != 0)
    {
        return 0;
    }
    return Result;
}

static void _bADS125XWriteReg(bDriverInterface_t *pdrv,uint8_t regaddr,uint8_t tx_data)
{

    uint8_t write_regaddr = ADS1256_CMD_WREG | (regaddr & 0x0F);

    _bADS125XSendByte(pdrv,write_regaddr);//向寄存器写入数据地址

    _bADS125XSendByte(pdrv,0x00);//写入数据的个数n-1

    _bADS125XSendByte(pdrv,tx_data); //写入1字节数据

}

static void _bADS125XReadReg(bDriverInterface_t *pdrv,uint8_t regaddr, uint8_t *rx_data)
{
    bDRIVER_GET_HALIF(_if, bADS125X_HalIf_t, pdrv);

    uint8_t read_regaddr = ADS1256_CMD_RREG | (regaddr & 0x0F);

    bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,0); //片选

    {
        uint32_t tick = bHalGetSysTick();
        while (bHalGpioReadPin(_if->drdy.port,_if->drdy.pin))
        {
            if (TICK_DIFF_BIT32(tick, bHalGetSysTick()) > MS2TICKS(1000))
            {
                bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1);
                return;
            }
        }
    }

    _bADS125XSendByte(pdrv,read_regaddr);//向寄存器读取数据地址

    _bADS125XSendByte(pdrv,0x00);//读取数据的个数n-1

    bHalDelayUs(200);

    *rx_data = _bADS125XReceiveByte(pdrv);//读取N字节数据

    bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1); //片选
}

/*
static void _bADS125XLoopCollectMode(bDriverInterface_t *pdrv)//连续采集模式
{
  bDRIVER_GET_HALIF(_if, bADS125X_HalIf_t, pdrv);

	bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,0); //片选
	_bADS125XSendByte(pdrv,ADS1256_CMD_SYNC);
	_bADS125XSendByte(pdrv,ADS1256_CMD_WAKEUP);
	_bADS125XSendByte(pdrv,ADS1256_CMD_RDATAC);
	bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1); //禁用
}
*/

static void _bADS125XInitConfig(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bADS125X_HalIf_t, pdrv);

    bHalDelayMs(1000); //延时2S,等待电源稳定

     bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,0); //片选

    _bADS125XSendByte(pdrv,ADS1256_CMD_SELFCAL);//自校准

    _bADS125XWriteReg(pdrv,ADS1256_STATUS,0x04);// 高位在前、校准、使用缓冲  

    _bADS125XWriteReg(pdrv,ADS1256_ADCON,ADS1256_GAIN_1);// 放大倍数1
    
    _bADS125XWriteReg(pdrv,ADS1256_MUX,ADS1256_MUXP_AIN0 | ADS1256_MUXN_AINCOM);//设置采集通道

    _bADS125XWriteReg(pdrv,ADS1256_DRATE,ADS1256_DRATE_30000SPS);//设置采集速率

    _bADS125XSendByte(pdrv,ADS1256_CMD_SELFCAL);//自校准

    bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1); //禁用

}

/*****************************************driver interface***************************************/

static int _bADS125XRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bADS125X_HalIf_t, pdrv);
    uint32_t sum = 0;

    bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,0); //片选
    _bADS125XSendByte(pdrv,ADS1256_CMD_RDATA);//读取ADC数据

    bHalDelayUs(100);
    sum |= (_bADS125XReceiveByte(pdrv) << 16);
    sum |= (_bADS125XReceiveByte(pdrv) << 8);
    sum |= _bADS125XReceiveByte(pdrv);

    bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1); //禁用

    memcpy(pbuf,&sum,len);

    return len;
}

static int _bADS125XCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bDRIVER_GET_HALIF(_if, bADS125X_HalIf_t, pdrv);
    bAds125xDrvData_t * Ads125xDataStruct = (bAds125xDrvData_t *)param;

    switch (cmd)
    {
    case ADS125X_SET_CHANNLE_CMD:
        {
            bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,0); //片选
            _bADS125XWriteReg(pdrv,ADS1256_MUX,Ads125xDataStruct->ads125x_channle);//设置采集通道
            bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1); //禁用
        }break;  
    case ADS125X_SET_GAIN_CMD:
        {
            bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,0); //片选
            _bADS125XWriteReg(pdrv,ADS1256_ADCON,Ads125xDataStruct->ads125x_gain);//设置放大倍数
            bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1); //禁用
        }break; 
    case ADS125X_SET_BRATE_CMD:
        {
            bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,0); //片选
            _bADS125XWriteReg(pdrv,ADS1256_DRATE,Ads125xDataStruct->ads125x_brate);//设置放大倍数
            bHalGpioWritePin(_if->_spi.cs.port ,_if->_spi.cs.pin,1); //禁用
        }break; 
    case ADS125X_READ_CHANNLE_CMD:
        {
            _bADS125XReadReg(pdrv,ADS1256_MUX,&Ads125xDataStruct->ads125x_channle);//读取通道配置
        }break; 
    case ADS125X_READ_GAIN_CMD:
        {
            _bADS125XReadReg(pdrv,ADS1256_ADCON,&Ads125xDataStruct->ads125x_gain);//读取放大配置
        }break;   
    case ADS125X_READ_BRATE_CMD:
        {
            _bADS125XReadReg(pdrv,ADS1256_DRATE,&Ads125xDataStruct->ads125x_brate);//读取采用率配置
        }break;    
    case ADS125X_READ_STATUS_CMD:
        {
            _bADS125XReadReg(pdrv,ADS1256_STATUS,&Ads125xDataStruct->ads125x_status);//读取状态配置
        }break;  
    default:
        break;
    }
    return 0;
}

static int _bADS125XOpen(bDriverInterface_t *pdrv)
{
    return 0;
}

static int _bADS125XClose(bDriverInterface_t *pdrv)
{
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup ADS125X_Exported_Functions
 * \{
 */
int bADS125X_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bADS125X_Init);
    pdrv->read  = _bADS125XRead;
    pdrv->write = NULL;
    pdrv->ctl   = _bADS125XCtl;
    pdrv->open  = _bADS125XOpen;
    pdrv->close = _bADS125XClose;
    pdrv->_private._p = &bADS125XRunInfo[pdrv->drv_no];

    _bADS125XInitConfig(pdrv);
    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_ADS125X, bADS125X_Init);

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

