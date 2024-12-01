/**
 *!
 * \file        b_drv_ili9341.c
 * \version     v0.0.1
 * \date        2020/03/02
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
#include "drivers/inc/b_drv_ili9341.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup ILI9341
 * \{
 */

/**
 * \defgroup ILI9341_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ILI9341_Private_Defines
 * \{
 */

#define DRIVER_NAME ILI9341

/**
 * \}
 */

/**
 * \defgroup ILI9341_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ILI9341_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bILI9341_HalIf_t, DRIVER_NAME);

static bILI9341Private_t bILI9341RunInfo[bDRIVER_HALIF_NUM(bILI9341_HalIf_t, DRIVER_NAME)];

#ifndef DISPLAY_CACHE_BUF_SIZE
#define DISPLAY_CACHE_BUF_SIZE (8)
#else
#if (DISPLAY_CACHE_BUF_SIZE == 0)
#error "invalid config ..."
#endif
#endif

static uint16_t bILI9341Buf[DISPLAY_CACHE_BUF_SIZE];

/**
 * \}
 */

/**
 * \defgroup ILI9341_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ILI9341_Private_Functions
 * \{
 */

static void _bLcdWriteData(bDriverInterface_t *pdrv, uint16_t dat)
{
    bDRIVER_GET_HALIF(_if, bILI9341_HalIf_t, pdrv);
    if (_if->if_type == LCD_IF_TYPE_RWADDR)
    {
        ((bLcdRWAddress_t *)_if->_if.rw_addr)->dat = dat;
    }
    else if (_if->if_type == LCD_IF_TYPE_IO)
    {
        bHalGpioWritePin(_if->_if._io.rs.port, _if->_if._io.rs.pin, 1);
        bHalGpioWritePin(_if->_if._io.rd.port, _if->_if._io.rd.pin, 1);
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 0);
        bHalGpioWritePort(_if->_if._io.data.port, dat);
        bHalGpioWritePin(_if->_if._io.wr.port, _if->_if._io.wr.pin, 0);
        bHalGpioWritePin(_if->_if._io.wr.port, _if->_if._io.wr.pin, 1);
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 1);
    }
    else if (_if->if_type == LCD_IF_TYPE_SPI)
    {
        bHalGpioWritePin(_if->_if._spi.rs.port, _if->_if._spi.rs.pin, 1);
        bHalGpioWritePin(_if->_if._spi._spi.cs.port, _if->_if._spi._spi.cs.pin, 0);
        bHalSpiSend(&_if->_if._spi._spi, (uint8_t *)&dat, 1);
        bHalGpioWritePin(_if->_if._spi._spi.cs.port, _if->_if._spi._spi.cs.pin, 1);
    }
}

static void _bLcdWriteCmd(bDriverInterface_t *pdrv, uint16_t cmd)
{
    bDRIVER_GET_HALIF(_if, bILI9341_HalIf_t, pdrv);
    if (_if->if_type == LCD_IF_TYPE_RWADDR)
    {
        ((bLcdRWAddress_t *)_if->_if.rw_addr)->reg = cmd;
    }
    else if (_if->if_type == LCD_IF_TYPE_IO)
    {
        bHalGpioWritePin(_if->_if._io.rs.port, _if->_if._io.rs.pin, 0);
        bHalGpioWritePin(_if->_if._io.rd.port, _if->_if._io.rd.pin, 1);
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 0);
        bHalGpioWritePort(_if->_if._io.data.port, cmd);
        bHalGpioWritePin(_if->_if._io.wr.port, _if->_if._io.wr.pin, 0);
        bHalGpioWritePin(_if->_if._io.wr.port, _if->_if._io.wr.pin, 1);
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 1);
    }
    else if (_if->if_type == LCD_IF_TYPE_SPI)
    {
        bHalGpioWritePin(_if->_if._spi.rs.port, _if->_if._spi.rs.pin, 0);
        bHalGpioWritePin(_if->_if._spi._spi.cs.port, _if->_if._spi._spi.cs.pin, 0);
        bHalSpiSend(&_if->_if._spi._spi, (uint8_t *)&cmd, 1);
        bHalGpioWritePin(_if->_if._spi._spi.cs.port, _if->_if._spi._spi.cs.pin, 1);
    }
}

static uint16_t _bLcdReadData(bDriverInterface_t *pdrv)
{
    uint16_t dat = 0;
    bDRIVER_GET_HALIF(_if, bILI9341_HalIf_t, pdrv);
    if (_if->if_type == LCD_IF_TYPE_RWADDR)
    {
        dat = ((bLcdRWAddress_t *)_if->_if.rw_addr)->dat;
    }
    else if (_if->if_type == LCD_IF_TYPE_IO)
    {
        bHalGpioConfig(_if->_if._io.data.port, _if->_if._io.data.pin, B_HAL_GPIO_INPUT,
                       B_HAL_GPIO_NOPULL);
        bHalGpioWritePin(_if->_if._io.rs.port, _if->_if._io.rs.pin, 1);
        bHalGpioWritePin(_if->_if._io.rd.port, _if->_if._io.rd.pin, 0);
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 0);
        bHalGpioWritePin(_if->_if._io.rd.port, _if->_if._io.rd.pin, 1);
        dat = bHalGpioReadPort(_if->_if._io.data.port);
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 1);
        bHalGpioConfig(_if->_if._io.data.port, _if->_if._io.data.pin, B_HAL_GPIO_OUTPUT,
                       B_HAL_GPIO_NOPULL);
    }
    return dat;
}

static void _bLcdWriteGramPre(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bILI9341_HalIf_t, pdrv);
    if (_if->if_type == LCD_IF_TYPE_RWADDR)
    {
        ;
    }
    else if (_if->if_type == LCD_IF_TYPE_IO)
    {
        bHalGpioWritePin(_if->_if._io.rs.port, _if->_if._io.rs.pin, 1);
        bHalGpioWritePin(_if->_if._io.rd.port, _if->_if._io.rd.pin, 1);
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 0);
    }
    else if (_if->if_type == LCD_IF_TYPE_SPI)
    {
        bHalGpioWritePin(_if->_if._spi.rs.port, _if->_if._spi.rs.pin, 1);
        bHalGpioWritePin(_if->_if._spi._spi.cs.port, _if->_if._spi._spi.cs.pin, 0);
    }
}

static void _bLcdWriteGramEnd(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bILI9341_HalIf_t, pdrv);
    if (_if->if_type == LCD_IF_TYPE_RWADDR)
    {
        ;
    }
    else if (_if->if_type == LCD_IF_TYPE_IO)
    {
        bHalGpioWritePin(_if->_if._io.cs.port, _if->_if._io.cs.pin, 1);
    }
    else if (_if->if_type == LCD_IF_TYPE_SPI)
    {
        bHalGpioWritePin(_if->_if._spi._spi.cs.port, _if->_if._spi._spi.cs.pin, 1);
    }
}

static void _bLcdWriteGRam(bDriverInterface_t *pdrv, uint16_t *pdat, uint32_t count, uint8_t inc)
{
    bDRIVER_GET_HALIF(_if, bILI9341_HalIf_t, pdrv);
    uint16_t j = 0;
    uint32_t i = 0;
    if (_if->if_type == LCD_IF_TYPE_RWADDR)
    {
        for (i = 0; i < count; i++)
        {
            ((bLcdRWAddress_t *)_if->_if.rw_addr)->dat = *pdat;
            if (inc)
            {
                pdat += 1;
            }
        }
    }
    else if (_if->if_type == LCD_IF_TYPE_IO)
    {
        for (i = 0; i < count; i++)
        {
            bHalGpioWritePort(_if->_if._io.data.port, *pdat);
            bHalGpioWritePin(_if->_if._io.wr.port, _if->_if._io.wr.pin, 0);
            bHalGpioWritePin(_if->_if._io.wr.port, _if->_if._io.wr.pin, 1);
            if (inc)
            {
                pdat += 1;
            }
        }
    }
    else if (_if->if_type == LCD_IF_TYPE_SPI)
    {
        uint8_t wlen = 0;
        for (i = 0; i < count;)
        {
            wlen = ((count - i) > DISPLAY_CACHE_BUF_SIZE) ? DISPLAY_CACHE_BUF_SIZE : (count - i);
            for (j = 0; j < wlen; j++)
            {
                bILI9341Buf[j] = *pdat;
                bILI9341Buf[j] = B_SWAP_16(bILI9341Buf[j]);
                if (inc)
                {
                    pdat += 1;
                }
            }
            bHalSpiSend(&_if->_if._spi._spi, (uint8_t *)bILI9341Buf, wlen * 2);
            i += wlen;
        }
    }
}

static int _bILI9341CheckId(bDriverInterface_t *pdrv)
{
    uint16_t id = 0x9341;
    bDRIVER_GET_HALIF(_if, bILI9341_HalIf_t, pdrv);
    if (_if->if_type == LCD_IF_TYPE_IO || _if->if_type == LCD_IF_TYPE_RWADDR)
    {
        _bLcdWriteCmd(pdrv, 0XD3);
        id = _bLcdReadData(pdrv);
        id = _bLcdReadData(pdrv);
        id = _bLcdReadData(pdrv);
        id <<= 8;
        id |= _bLcdReadData(pdrv);
    }
    else if (_if->if_type == LCD_IF_TYPE_SPI)
    {
    }
    return ((id == 0x9341) ? 0 : -1);
}

static void _bILI9341SetCursor(bDriverInterface_t *pdrv, uint16_t Xpos, uint16_t Ypos)
{
    _bLcdWriteCmd(pdrv, 0X2A);
    _bLcdWriteData(pdrv, Xpos >> 8);
    _bLcdWriteData(pdrv, Xpos & 0XFF);
    _bLcdWriteCmd(pdrv, 0X2B);
    _bLcdWriteData(pdrv, Ypos >> 8);
    _bLcdWriteData(pdrv, Ypos & 0XFF);
    _bLcdWriteCmd(pdrv, 0X2C);
}

static int _bILI9341FillRect(bDriverInterface_t *pdrv, uint16_t x1, uint16_t y1, uint16_t x2,
                             uint16_t y2, uint16_t color)
{
    uint16_t tmp = 0;
    bDRIVER_GET_PRIVATE(prv, bILI9341Private_t, pdrv);
    if (x1 > x2)
    {
        tmp = x1;
        x1  = x2;
        x2  = tmp;
    }
    if (y1 > y2)
    {
        tmp = y1;
        y1  = y2;
        y2  = tmp;
    }

    if (x2 >= (prv->width) || y2 >= (prv->length))
    {
        return -1;
    }

    _bLcdWriteCmd(pdrv, 0x2a);
    _bLcdWriteData(pdrv, x1 >> 8);
    _bLcdWriteData(pdrv, x1);
    _bLcdWriteData(pdrv, x2 >> 8);
    _bLcdWriteData(pdrv, x2);
    _bLcdWriteCmd(pdrv, 0x2b);
    _bLcdWriteData(pdrv, y1 >> 8);
    _bLcdWriteData(pdrv, y1);
    _bLcdWriteData(pdrv, y2 >> 8);
    _bLcdWriteData(pdrv, y2);
    _bLcdWriteCmd(pdrv, 0x2C);

    _bLcdWriteGramPre(pdrv);
    _bLcdWriteGRam(pdrv, &color, ((x2 - x1 + 1) * (y2 - y1 + 1)), 0);
    _bLcdWriteGramEnd(pdrv);
    return 0;
}

static int _bILI9341FillBmp(bDriverInterface_t *pdrv, uint16_t x1, uint16_t y1, uint16_t x2,
                            uint16_t y2, uint8_t *color)
{
    uint16_t tmp = 0;
    bDRIVER_GET_PRIVATE(prv, bILI9341Private_t, pdrv);
    if (x1 > x2)
    {
        tmp = x1;
        x1  = x2;
        x2  = tmp;
    }
    if (y1 > y2)
    {
        tmp = y1;
        y1  = y2;
        y2  = tmp;
    }

    if (x2 >= (prv->width) || y2 >= (prv->length))
    {
        return -1;
    }

    _bLcdWriteCmd(pdrv, 0x2a);
    _bLcdWriteData(pdrv, x1 >> 8);
    _bLcdWriteData(pdrv, x1);
    _bLcdWriteData(pdrv, x2 >> 8);
    _bLcdWriteData(pdrv, x2);
    _bLcdWriteCmd(pdrv, 0x2b);
    _bLcdWriteData(pdrv, y1 >> 8);
    _bLcdWriteData(pdrv, y1);
    _bLcdWriteData(pdrv, y2 >> 8);
    _bLcdWriteData(pdrv, y2);
    _bLcdWriteCmd(pdrv, 0x2C);
    _bLcdWriteGramPre(pdrv);
    _bLcdWriteGRam(pdrv, (uint16_t *)color, ((x2 - x1 + 1) * (y2 - y1 + 1)), 1);
    _bLcdWriteGramEnd(pdrv);
    return 0;
}

static int _bILI9341Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    int retval = -1;
    bDRIVER_GET_PRIVATE(prv, bILI9341Private_t, pdrv);
    switch (cmd)
    {
        case bCMD_FILL_RECT:
        {
            bLcdRectInfo_t *pinfo = (bLcdRectInfo_t *)param;
            if (param == NULL)
            {
                return -1;
            }
            retval =
                _bILI9341FillRect(pdrv, pinfo->x1, pinfo->y1, pinfo->x2, pinfo->y2, pinfo->color);
        }
        break;
        case bCMD_FILL_BMP:
        {
            bLcdBmpInfo_t *pinfo = (bLcdBmpInfo_t *)param;
            if (param == NULL)
            {
                return -1;
            }
            retval =
                _bILI9341FillBmp(pdrv, pinfo->x1, pinfo->y1, pinfo->x2, pinfo->y2, pinfo->color);
        }
        break;
        case bCMD_SET_SIZE:
        {
            bLcdSize_t *pinfo = (bLcdSize_t *)param;
            if (param == NULL || prv == NULL)
            {
                return -1;
            }
            prv->width  = pinfo->width;
            prv->length = pinfo->length;
            retval      = 0;
        }
        break;
        default:
            break;
    }
    return retval;
}

static int _bILI9341Write(bDriverInterface_t *pdrv, uint32_t addr, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_PRIVATE(prv, bILI9341Private_t, pdrv);
    uint16_t     x      = addr % (prv->width);
    uint16_t     y      = addr / (prv->width);
    bLcdWrite_t *pcolor = (bLcdWrite_t *)pbuf;
    if (y >= (prv->length) || pbuf == NULL || len < sizeof(bLcdWrite_t))
    {
        return -1;
    }
    _bILI9341SetCursor(pdrv, x, y);
    _bLcdWriteGRam(pdrv, &pcolor->color, 1, 0);
    return 2;
}

/**
 * \}
 */

/**
 * \addtogroup ILI9341_Exported_Functions
 * \{
 */

int bILI9341_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bILI9341_Init);
    pdrv->write       = _bILI9341Write;
    pdrv->ctl         = _bILI9341Ctl;
    pdrv->_private._p = &bILI9341RunInfo[pdrv->drv_no];

    bILI9341RunInfo[pdrv->drv_no].width  = 240;  // default
    bILI9341RunInfo[pdrv->drv_no].length = 320;  // default

    if (((bILI9341_HalIf_t *)pdrv->hal_if)->reset.port != B_HAL_GPIO_INVALID &&
        ((bILI9341_HalIf_t *)pdrv->hal_if)->reset.pin != B_HAL_PIN_INVALID)
    {
        bHalGpioWritePin(((bILI9341_HalIf_t *)pdrv->hal_if)->reset.port,
                         ((bILI9341_HalIf_t *)pdrv->hal_if)->reset.pin, 0);
        bHalDelayMs(100);
        bHalGpioWritePin(((bILI9341_HalIf_t *)pdrv->hal_if)->reset.port,
                         ((bILI9341_HalIf_t *)pdrv->hal_if)->reset.pin, 1);
        bHalDelayMs(100);
    }

    if (_bILI9341CheckId(pdrv) < 0)
    {
        return -1;
    }
    _bLcdWriteCmd(pdrv, 0xCF);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0xC1);
    _bLcdWriteData(pdrv, 0X30);
    _bLcdWriteCmd(pdrv, 0xED);
    _bLcdWriteData(pdrv, 0x64);
    _bLcdWriteData(pdrv, 0x03);
    _bLcdWriteData(pdrv, 0X12);
    _bLcdWriteData(pdrv, 0X81);
    _bLcdWriteCmd(pdrv, 0xE8);
    _bLcdWriteData(pdrv, 0x85);
    _bLcdWriteData(pdrv, 0x10);
    _bLcdWriteData(pdrv, 0x7A);
    _bLcdWriteCmd(pdrv, 0xCB);
    _bLcdWriteData(pdrv, 0x39);
    _bLcdWriteData(pdrv, 0x2C);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x34);
    _bLcdWriteData(pdrv, 0x02);
    _bLcdWriteCmd(pdrv, 0xF7);
    _bLcdWriteData(pdrv, 0x20);
    _bLcdWriteCmd(pdrv, 0xEA);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteCmd(pdrv, 0xC0);
    _bLcdWriteData(pdrv, 0x1B);
    _bLcdWriteCmd(pdrv, 0xC1);
    _bLcdWriteData(pdrv, 0x01);
    _bLcdWriteCmd(pdrv, 0xC5);
    _bLcdWriteData(pdrv, 0x30);
    _bLcdWriteData(pdrv, 0x30);
    _bLcdWriteCmd(pdrv, 0xC7);
    _bLcdWriteData(pdrv, 0XB7);
    _bLcdWriteCmd(pdrv, 0x36);
    _bLcdWriteData(pdrv, 0x08);
    _bLcdWriteCmd(pdrv, 0x3A);
    _bLcdWriteData(pdrv, 0x55);
    _bLcdWriteCmd(pdrv, 0xB1);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x18);
    _bLcdWriteCmd(pdrv, 0xB6);
    _bLcdWriteData(pdrv, 0x0A);
    _bLcdWriteData(pdrv, 0xA2);
    _bLcdWriteCmd(pdrv, 0xF2);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteCmd(pdrv, 0x26);
    _bLcdWriteData(pdrv, 0x01);
    // POSITIVE GAMMA CORRECTION
    _bLcdWriteCmd(pdrv, 0xE0);
    _bLcdWriteData(pdrv, 0x0F);
    _bLcdWriteData(pdrv, 0x31);
    _bLcdWriteData(pdrv, 0x2B);
    _bLcdWriteData(pdrv, 0x0C);
    _bLcdWriteData(pdrv, 0x0E);
    _bLcdWriteData(pdrv, 0x08);
    _bLcdWriteData(pdrv, 0x4E);
    _bLcdWriteData(pdrv, 0xF1);
    _bLcdWriteData(pdrv, 0x37);
    _bLcdWriteData(pdrv, 0x07);
    _bLcdWriteData(pdrv, 0x10);
    _bLcdWriteData(pdrv, 0x03);
    _bLcdWriteData(pdrv, 0x0E);
    _bLcdWriteData(pdrv, 0x09);
    _bLcdWriteData(pdrv, 0x00);

    // NEGATIVE GAMMA CORRECTION
    _bLcdWriteCmd(pdrv, 0xE1);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x0E);
    _bLcdWriteData(pdrv, 0x14);
    _bLcdWriteData(pdrv, 0x03);
    _bLcdWriteData(pdrv, 0x11);
    _bLcdWriteData(pdrv, 0x07);
    _bLcdWriteData(pdrv, 0x31);
    _bLcdWriteData(pdrv, 0xC1);
    _bLcdWriteData(pdrv, 0x48);
    _bLcdWriteData(pdrv, 0x08);
    _bLcdWriteData(pdrv, 0x0F);
    _bLcdWriteData(pdrv, 0x0C);
    _bLcdWriteData(pdrv, 0x31);
    _bLcdWriteData(pdrv, 0x36);
    _bLcdWriteData(pdrv, 0x0F);
    _bLcdWriteCmd(pdrv, 0x2B);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x01);
    _bLcdWriteData(pdrv, 0x3f);
    _bLcdWriteCmd(pdrv, 0x2A);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0x00);
    _bLcdWriteData(pdrv, 0xef);
    _bLcdWriteCmd(pdrv, 0x11);
    bHalDelayMs(120);
    _bLcdWriteCmd(pdrv, 0x29);
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_ILI9341, bILI9341_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif
/**
 * \}
 */

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
