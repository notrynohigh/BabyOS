/**
 *!
 * \file        b_drv_enc28j60.c
 * \version     v0.0.1
 * \date        2023/03/25
 * \author      babyos
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 babyos
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
#include "drivers/inc/b_drv_enc28j60.h"

#include <string.h>

#include "core/inc/b_task.h"
#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup ENC28J60
 * \{
 */

/**
 * \defgroup ENC28J60_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ENC28J60_Private_Defines
 * \{
 */
#define DRIVER_NAME ENC28J60

#define ENC28J60_DEBUG_ENABLE (0)

#define ADDR_MASK 0X1F
#define BANK_MASK 0X60
#define SPRD_MASK 0X80

#define EIE 0x1B
#define EIR 0x1C
#define ESTAT 0x1D
#define ECON2 0x1E
#define ECON1 0x1F

#define ERDPTL (0x00 | 0x00)
#define ERDPTH (0x01 | 0x00)
#define EWRPTL (0x02 | 0x00)
#define EWRPTH (0x03 | 0x00)
#define ETXSTL (0x04 | 0x00)
#define ETXSTH (0x05 | 0x00)
#define ETXNDL (0x06 | 0x00)
#define ETXNDH (0x07 | 0x00)
#define ERXSTL (0x08 | 0x00)
#define ERXSTH (0x09 | 0x00)
#define ERXNDL (0x0A | 0x00)
#define ERXNDH (0x0B | 0x00)
#define ERXRDPTL (0x0C | 0x00)
#define ERXRDPTH (0x0D | 0x00)
#define ERXWRPTL (0x0E | 0x00)
#define ERXWRPTH (0x0F | 0x00)
#define EDMASTL (0x10 | 0x00)
#define EDMASTH (0x11 | 0x00)
#define EDMANDL (0x12 | 0x00)
#define EDMANDH (0x13 | 0x00)
#define EDMADSTL (0x14 | 0x00)
#define EDMADSTH (0x15 | 0x00)
#define EDMACSL (0x16 | 0x00)
#define EDMACSH (0x17 | 0x00)

#define EHT0 (0x00 | 0x20)
#define EHT1 (0x01 | 0x20)
#define EHT2 (0x02 | 0x20)
#define EHT3 (0x03 | 0x20)
#define EHT4 (0x04 | 0x20)
#define EHT5 (0x05 | 0x20)
#define EHT6 (0x06 | 0x20)
#define EHT7 (0x07 | 0x20)
#define EPMM0 (0x08 | 0x20)
#define EPMM1 (0x09 | 0x20)
#define EPMM2 (0x0A | 0x20)
#define EPMM3 (0x0B | 0x20)
#define EPMM4 (0x0C | 0x20)
#define EPMM5 (0x0D | 0x20)
#define EPMM6 (0x0E | 0x20)
#define EPMM7 (0x0F | 0x20)
#define EPMCSL (0x10 | 0x20)
#define EPMCSH (0x11 | 0x20)
#define EPMOL (0x14 | 0x20)
#define EPMOH (0x15 | 0x20)
#define EWOLIE (0x16 | 0x20)
#define EWOLIR (0x17 | 0x20)
#define ERXFCON (0x18 | 0x20)
#define EPKTCNT (0x19 | 0x20)

#define MACON1 (0x00 | 0x40 | 0x80)
#define MACON2 (0x01 | 0x40 | 0x80)
#define MACON3 (0x02 | 0x40 | 0x80)
#define MACON4 (0x03 | 0x40 | 0x80)
#define MABBIPG (0x04 | 0x40 | 0x80)
#define MAIPGL (0x06 | 0x40 | 0x80)
#define MAIPGH (0x07 | 0x40 | 0x80)
#define MACLCON1 (0x08 | 0x40 | 0x80)
#define MACLCON2 (0x09 | 0x40 | 0x80)
#define MAMXFLL (0x0A | 0x40 | 0x80)
#define MAMXFLH (0x0B | 0x40 | 0x80)
#define MAPHSUP (0x0D | 0x40 | 0x80)
#define MICON (0x11 | 0x40 | 0x80)
#define MICMD (0x12 | 0x40 | 0x80)
#define MIREGADR (0x14 | 0x40 | 0x80)
#define MIWRL (0x16 | 0x40 | 0x80)
#define MIWRH (0x17 | 0x40 | 0x80)
#define MIRDL (0x18 | 0x40 | 0x80)
#define MIRDH (0x19 | 0x40 | 0x80)

#define MAADR1 (0x00 | 0x60 | 0x80)
#define MAADR0 (0x01 | 0x60 | 0x80)
#define MAADR3 (0x02 | 0x60 | 0x80)
#define MAADR2 (0x03 | 0x60 | 0x80)
#define MAADR5 (0x04 | 0x60 | 0x80)
#define MAADR4 (0x05 | 0x60 | 0x80)
#define EBSTSD (0x06 | 0x60)
#define EBSTCON (0x07 | 0x60)
#define EBSTCSL (0x08 | 0x60)
#define EBSTCSH (0x09 | 0x60)
#define MISTAT (0x0A | 0x60 | 0x80)
#define EREVID (0x12 | 0x60)
#define ECOCON (0x15 | 0x60)
#define EFLOCON (0x17 | 0x60)
#define EPAUSL (0x18 | 0x60)
#define EPAUSH (0x19 | 0x60)

#define PHCON1 0x00
#define PHSTAT1 0x01
#define PHHID1 0x02
#define PHHID2 0x03
#define PHCON2 0x10
#define PHSTAT2 0x11
#define PHIE 0x12
#define PHIR 0x13
#define PHLCON 0x14

#define ERXFCON_UCEN 0x80
#define ERXFCON_ANDOR 0x40
#define ERXFCON_CRCEN 0x20
#define ERXFCON_PMEN 0x10
#define ERXFCON_MPEN 0x08
#define ERXFCON_HTEN 0x04
#define ERXFCON_MCEN 0x02
#define ERXFCON_BCEN 0x01

#define EIE_INTIE 0x80
#define EIE_PKTIE 0x40
#define EIE_DMAIE 0x20
#define EIE_LINKIE 0x10
#define EIE_TXIE 0x08
#define EIE_WOLIE 0x04
#define EIE_TXERIE 0x02
#define EIE_RXERIE 0x01

#define EIR_PKTIF 0x40
#define EIR_DMAIF 0x20
#define EIR_LINKIF 0x10
#define EIR_TXIF 0x08
#define EIR_WOLIF 0x04
#define EIR_TXERIF 0x02
#define EIR_RXERIF 0x01

#define ESTAT_INT 0x80
#define ESTAT_LATECOL 0x10
#define ESTAT_RXBUSY 0x04
#define ESTAT_TXABRT 0x02
#define ESTAT_CLKRDY 0x01

#define ECON2_AUTOINC 0x80
#define ECON2_PKTDEC 0x40
#define ECON2_PWRSV 0x20
#define ECON2_VRPS 0x08

#define ECON1_TXRST 0x80
#define ECON1_RXRST 0x40
#define ECON1_DMAST 0x20
#define ECON1_CSUMEN 0x10
#define ECON1_TXRTS 0x08
#define ECON1_RXEN 0x04
#define ECON1_BSEL1 0x02
#define ECON1_BSEL0 0x01

#define MACON1_LOOPBK 0x10
#define MACON1_TXPAUS 0x08
#define MACON1_RXPAUS 0x04
#define MACON1_PASSALL 0x02
#define MACON1_MARXEN 0x01

#define MACON2_MARST 0x80
#define MACON2_RNDRST 0x40
#define MACON2_MARXRST 0x08
#define MACON2_RFUNRST 0x04
#define MACON2_MATXRST 0x02
#define MACON2_TFUNRST 0x01

#define MACON3_PADCFG2 0x80
#define MACON3_PADCFG1 0x40
#define MACON3_PADCFG0 0x20
#define MACON3_TXCRCEN 0x10
#define MACON3_PHDRLEN 0x08
#define MACON3_HFRMEN 0x04
#define MACON3_FRMLNEN 0x02
#define MACON3_FULDPX 0x01

#define MICMD_MIISCAN 0x02
#define MICMD_MIIRD 0x01

#define MISTAT_NVALID 0x04
#define MISTAT_SCAN 0x02
#define MISTAT_BUSY 0x01

#define PHCON1_PRST 0x8000
#define PHCON1_PLOOPBK 0x4000
#define PHCON1_PPWRSV 0x0800
#define PHCON1_PDPXMD 0x0100

#define PHSTAT1_PFDPX 0x1000
#define PHSTAT1_PHDPX 0x0800
#define PHSTAT1_LLSTAT 0x0004
#define PHSTAT1_JBSTAT 0x0002

#define PHCON2_FRCLINK 0x4000
#define PHCON2_TXDIS 0x2000
#define PHCON2_JABBER 0x0400
#define PHCON2_HDLDIS 0x0100

#define PKTCTRL_PHUGEEN 0x08
#define PKTCTRL_PPADEN 0x04
#define PKTCTRL_PCRCEN 0x02
#define PKTCTRL_POVERRIDE 0x01

#define ENC28J60_READ_CTRL_REG 0x00
#define ENC28J60_READ_BUF_MEM 0x3A
#define ENC28J60_WRITE_CTRL_REG 0x40
#define ENC28J60_WRITE_BUF_MEM 0x7A
#define ENC28J60_BIT_FIELD_SET 0x80
#define ENC28J60_BIT_FIELD_CLR 0xA0
#define ENC28J60_SOFT_RESET 0xFF

#define RXSTART_INIT 0x0
#define RXSTOP_INIT (0x1FFF - 1518 - 1)
#define TXSTART_INIT (0x1FFF - 1518)
#define TXSTOP_INIT 0x1FFF
#define MAX_FRAMELEN 1518

/**
 * \}
 */

/**
 * \defgroup ENC28J60_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ENC28J60_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bENC28J60_HalIf_t, DRIVER_NAME);

static bENC28J60Private_t bENC28J60RunInfo[bDRIVER_HALIF_NUM(bENC28J60_HalIf_t, DRIVER_NAME)];
static bTaskAttr_t        bENC28J60TaskAttr[bDRIVER_HALIF_NUM(bENC28J60_HalIf_t, DRIVER_NAME)];
/**
 * \}
 */

/**
 * \defgroup ENC28J60_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ENC28J60_Private_Functions
 * \{
 */

static uint8_t _bENC28J60ReadOp(bDriverInterface_t *pdrv, uint8_t op, uint8_t address)
{
    uint8_t dat = 0;
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 0);
    dat = op | (address & ADDR_MASK);
    bHalSpiTransfer(&_if->_spi, dat);
    dat = bHalSpiTransfer(&_if->_spi, 0xFF);
    if (address & 0x80)
    {
        dat = bHalSpiTransfer(&_if->_spi, 0xFF);
    }
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 1);
    return dat;
}

static void _bENC28J60WriteOp(bDriverInterface_t *pdrv, uint8_t op, uint8_t address, uint8_t data)
{
    uint8_t dat = 0;
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 0);
    dat = op | (address & ADDR_MASK);
    bHalSpiTransfer(&_if->_spi, dat);
    dat = data;
    bHalSpiTransfer(&_if->_spi, dat);
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 1);
}

static int _bENC28J60ReadBuffer(bDriverInterface_t *pdrv, uint8_t *pdata, uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 0);
    bHalSpiTransfer(&_if->_spi, ENC28J60_READ_BUF_MEM);
    if (bHalSpiReceive(&_if->_spi, pdata, len) != 0)
    {
        bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 1);
        return -1;
    }
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 1);
    return 0;
}

static void _bENC28J60WriteBuffer(bDriverInterface_t *pdrv, uint8_t *pdata, uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 0);
    bHalSpiTransfer(&_if->_spi, ENC28J60_WRITE_BUF_MEM);
    bHalSpiSend(&_if->_spi, pdata, len);
    bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 1);
}

static void _bENC28J60SetBank(bDriverInterface_t *pdrv, uint8_t address)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bENC28J60Private_t, pdrv);
    if ((address & BANK_MASK) != _priv->cur_bank)
    {
        _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, ECON1, (ECON1_BSEL1 | ECON1_BSEL0));
        _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_SET, ECON1, (address & BANK_MASK) >> 5);
        _priv->cur_bank = (address & BANK_MASK);
    }
}

static uint8_t _bENC28J60ReadReg(bDriverInterface_t *pdrv, uint8_t address)
{
    uint8_t retry = 3;
    uint8_t val;
    while (retry--)
    {
        _bENC28J60SetBank(pdrv, address);
        val = _bENC28J60ReadOp(pdrv, ENC28J60_READ_CTRL_REG, address);
        if (address == ESTAT)
        {
            if (val & ESTAT_CLKRDY)
                break;
        }
        else
        {
            break;
        }
    }
    return val;
}

static void _bENC28J60WriteReg(bDriverInterface_t *pdrv, uint8_t address, uint8_t data)
{
    _bENC28J60SetBank(pdrv, address);
    _bENC28J60WriteOp(pdrv, ENC28J60_WRITE_CTRL_REG, address, data);
}

static void _bENC28J60WriteReg16(bDriverInterface_t *pdrv, uint8_t addr_low, uint8_t addr_high,
                                 uint16_t val)
{
    _bENC28J60WriteReg(pdrv, addr_low, val & 0xFF);
    _bENC28J60WriteReg(pdrv, addr_high, val >> 8);
}

static uint16_t _bENC28J60ReadReg16(bDriverInterface_t *pdrv, uint8_t addr_low, uint8_t addr_high)
{
    return (_bENC28J60ReadReg(pdrv, addr_high) << 8) | _bENC28J60ReadReg(pdrv, addr_low);
}

static void _bENC28J60WritePhy(bDriverInterface_t *pdrv, uint8_t address, uint16_t data)
{
    uint8_t timeout = 0;
    _bENC28J60WriteReg(pdrv, MIREGADR, address);
    _bENC28J60WriteReg(pdrv, MIWRL, (data & 0Xff));
    _bENC28J60WriteReg(pdrv, MIWRH, (data & 0Xff00) >> 8);
    while (_bENC28J60ReadReg(pdrv, MISTAT) & MISTAT_BUSY)
    {
        bHalDelayUs(1000);
        timeout++;
        if (timeout > 100)
        {
            return;
        }
    }
}

static uint16_t _bENC28J60ReadPhy(bDriverInterface_t *pdrv, uint8_t address)
{
    uint8_t  timeout = 0, tmp = 0;
    uint16_t regval = 0;

    tmp = _bENC28J60ReadReg(pdrv, MICMD);
    _bENC28J60WriteReg(pdrv, MIREGADR, address);
    _bENC28J60WriteReg(pdrv, MICMD, MICMD_MIIRD | tmp);
    while (_bENC28J60ReadReg(pdrv, MISTAT) & MISTAT_BUSY)
    {
        bHalDelayUs(1000);
        timeout++;
        if (timeout > 100)
        {
            break;
        }
    }
    _bENC28J60WriteReg(pdrv, MICMD, tmp & (~MICMD_MIIRD));
    regval = _bENC28J60ReadReg(pdrv, MIRDL);
    regval |= (_bENC28J60ReadReg(pdrv, MIRDH) << 8);
    return regval;
}

static uint8_t _bENC28J60CheckLinkUp(bDriverInterface_t *pdrv)
{
    uint16_t phstat1;
    phstat1 = _bENC28J60ReadPhy(pdrv, PHSTAT1);
    if (phstat1 & PHSTAT1_LLSTAT)
    {
        return 1;
    }
    return 0;
}

static uint16_t _bENC28J60PacketSend(bDriverInterface_t *pdrv, uint8_t *packet, uint16_t len)
{
    uint16_t timeout       = 0;
    uint8_t  macon3_backup = 0;
    uint8_t  retry_cnt     = 3;    // Retry count for TX operation
    uint16_t link_wait_ms  = 500;  // Link recovery wait time (ms) - corrected type
    uint8_t  link_retry    = 10;   // Link detection retry count

    // Boundary check: Validate packet length
    if (packet == NULL || len == 0 || len > (MAX_FRAMELEN + 100))
    {
        b_log("ENC28J60 TX Invalid Packet: len=%d\r\n", len);
        return 0;
    }

    // Retry loop: Core auto-recovery logic
    while (retry_cnt--)
    {
        // 1. Clear previous TX error flags
        _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, EIR, EIR_TXERIF | EIR_TXIF);
        _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, ESTAT, ESTAT_TXABRT);

        // 2. Wait for current TX request completion (prevent bus busy)
        timeout = 0;
        while ((_bENC28J60ReadReg(pdrv, ECON1) & ECON1_TXRTS) != 0)
        {
            bHalDelayUs(1000);
            timeout++;
            if (timeout > 250)
            {
                b_log("ENC28J60 TX Busy Timeout (retry %d left)\r\n", retry_cnt);
                // Force clear TX request
                _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
                bHalDelayUs(100);
                _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
                break;
            }
        }

        // 3. Configure TX buffer address
        _bENC28J60WriteReg16(pdrv, EWRPTL, EWRPTH, TXSTART_INIT);
        uint16_t total_len = len + 1;
        _bENC28J60WriteReg16(pdrv, ETXNDL, ETXNDH, TXSTART_INIT + total_len);

        // 4. Handle jumbo frame (larger than MAX_FRAMELEN)
        if (len > MAX_FRAMELEN)
        {
            macon3_backup = _bENC28J60ReadReg(pdrv, MACON3);
            _bENC28J60WriteReg(pdrv, MACON3, macon3_backup | MACON3_HFRMEN);
            _bENC28J60WriteOp(pdrv, ENC28J60_WRITE_BUF_MEM, 0, PKTCTRL_PHUGEEN);
        }
        else
        {
            _bENC28J60WriteOp(pdrv, ENC28J60_WRITE_BUF_MEM, 0, PKTCTRL_PPADEN | PKTCTRL_PCRCEN);
        }

        // 5. Write packet to TX buffer
        _bENC28J60WriteBuffer(pdrv, packet, len);

        // 6. Trigger TX request
        _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRTS);

        // 7. Wait for TX completion/timeout
        timeout = 0;
        while ((_bENC28J60ReadReg(pdrv, EIR) & EIR_TXIF) == 0)
        {
            bHalDelayUs(100);
            timeout++;
            if (timeout > 100)
                break;
        }

        // 8. Read TX status registers
        uint8_t estat = _bENC28J60ReadReg(pdrv, ESTAT);
        uint8_t eir   = _bENC28J60ReadReg(pdrv, EIR);

        // 9. Error detection and recovery
        if ((estat & ESTAT_TXABRT) || (eir & EIR_TXERIF))
        {
            // Clear error status
            _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRTS);
            _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, EIR, EIR_TXERIF | EIR_TXIF);

            // Restore MACON3 register
            if (len > MAX_FRAMELEN)
            {
                _bENC28J60WriteReg(pdrv, MACON3, macon3_backup);
            }

            b_log("ENC28J60 TX Error (retry %d left): ESTAT=0x%02X, EIR=0x%02X\r\n", retry_cnt,
                  estat, eir);

            // Auto-recovery 1: Check link status, wait for reconnection
            if (!_bENC28J60CheckLinkUp(pdrv))
            {
                b_log("ENC28J60 Link Down, waiting for reconnect...\r\n");
                uint8_t link_timeout = 0;
                while (!_bENC28J60CheckLinkUp(pdrv) && link_timeout < link_retry)
                {
                    bHalDelayMs(link_wait_ms);
                    link_timeout++;
                }
                if (link_timeout >= link_retry)
                {
                    b_log("ENC28J60 Link Reconnect Timeout\r\n");
                    return 0;  // Link recovery failed, abort retry
                }
                b_log("ENC28J60 Link Reconnected\r\n");
            }

            // Auto-recovery 2: Soft reset TX module
            _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRST);
            bHalDelayUs(100);
            _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
            bHalDelayMs(10);  // Short delay after reset

            continue;  // Retry transmission
        }

        // 10. TX success, restore register and return length
        if (len > MAX_FRAMELEN)
        {
            _bENC28J60WriteReg(pdrv, MACON3, macon3_backup);
        }
#if ENC28J60_DEBUG_ENABLE
        b_log("enc28j60 write.%d.\r\n", len);
#endif
        return len;
    }

    // All retries failed
    b_log("ENC28J60 TX All Retry Failed (total retry: 3)\r\n");
    return 0;
}

static void _bENC28J60PacketRead(bDriverInterface_t *pdrv, void **p, uint32_t *plen)
{
    uint16_t rxstat      = 0;
    uint16_t len         = 0;
    void    *buf         = NULL;
    void    *payload     = NULL;
    uint32_t payload_len = 0;
    uint8_t  timeout     = 0;
    bDRIVER_GET_PRIVATE(_priv, bENC28J60Private_t, pdrv);
    if (_priv->list_opt.m_create == NULL)
    {
        return;
    }
    if (_bENC28J60ReadReg(pdrv, EPKTCNT) == 0)
    {
        return;
    }
    while ((_bENC28J60ReadReg(pdrv, ESTAT) & ESTAT_RXBUSY) != 0)
    {
        bHalDelayUs(1000);
        timeout++;
        if (timeout > 250)
        {
            return;
        }
    }
    _bENC28J60WriteReg(pdrv, ERDPTL, (_priv->next_packet_ptr) & 0xff);
    _bENC28J60WriteReg(pdrv, ERDPTH, ((_priv->next_packet_ptr) & 0xff00) >> 8);
    _priv->next_packet_ptr = _bENC28J60ReadOp(pdrv, ENC28J60_READ_BUF_MEM, 0);
    _priv->next_packet_ptr |= _bENC28J60ReadOp(pdrv, ENC28J60_READ_BUF_MEM, 0) << 8;

    len = _bENC28J60ReadOp(pdrv, ENC28J60_READ_BUF_MEM, 0);
    len |= _bENC28J60ReadOp(pdrv, ENC28J60_READ_BUF_MEM, 0) << 8;

    if (len <= 4)
    {
        len = 0;
    }
    else
    {
        len -= 4;
    }
#if ENC28J60_DEBUG_ENABLE
    b_log("enc28j60 read.%d.\r\n", len);
#endif
    rxstat = _bENC28J60ReadOp(pdrv, ENC28J60_READ_BUF_MEM, 0);
    rxstat |= _bENC28J60ReadOp(pdrv, ENC28J60_READ_BUF_MEM, 0) << 8;

    if ((rxstat & 0x80) == 0)
    {
        len = 0;
    }
    else
    {
        _priv->list_opt.m_create(len, &buf);
        if (buf != NULL)
        {
            _priv->list_opt.m_payload(buf, &payload, &payload_len);
            if (_bENC28J60ReadBuffer(pdrv, payload, len) == 0)
            {
                *p    = buf;
                *plen = len;
            }
            else
            {
                _priv->list_opt.m_free(buf);
                *p    = NULL;
                *plen = 0;
            }
        }
    }
    _bENC28J60WriteReg(pdrv, ERXRDPTL, (_priv->next_packet_ptr) & 0xff);
    _bENC28J60WriteReg(pdrv, ERXRDPTH, ((_priv->next_packet_ptr) & 0xff00) >> 8);

    _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
}

static uint16_t _bENC28J60GetDuplex(bDriverInterface_t *pdrv)
{
    uint16_t temp;
    temp = _bENC28J60ReadPhy(pdrv, PHCON1) & 0X0100;
    return (temp >> 8);
}

static void _bENC28J60PrintStatus(bDriverInterface_t *pdrv)
{
#if ENC28J60_DEBUG_ENABLE
    b_log("=== ENC28J60 Status ===\r\n");
    b_log("ESTAT: 0x%02X (TXABRT: %d)\r\n", _bENC28J60ReadReg(pdrv, ESTAT),
          (_bENC28J60ReadReg(pdrv, ESTAT) & ESTAT_TXABRT) ? 1 : 0);
    b_log("EIR:   0x%02X (TXERIF: %d, TXIF: %d)\r\n", _bENC28J60ReadReg(pdrv, EIR),
          (_bENC28J60ReadReg(pdrv, EIR) & EIR_TXERIF) ? 1 : 0,
          (_bENC28J60ReadReg(pdrv, EIR) & EIR_TXIF) ? 1 : 0);
    b_log("ECON1: 0x%02X (TXRTS: %d, RXEN: %d)\r\n", _bENC28J60ReadReg(pdrv, ECON1),
          (_bENC28J60ReadReg(pdrv, ECON1) & ECON1_TXRTS) ? 1 : 0,
          (_bENC28J60ReadReg(pdrv, ECON1) & ECON1_RXEN) ? 1 : 0);
    b_log("Link:  %d, Duplex: %s\r\n", _bENC28J60CheckLinkUp(pdrv),
          _bENC28J60GetDuplex(pdrv) ? "Full" : "Half");
    b_log("========================\r\n");
#endif
}

static int _bENC28J60Write(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bENC28J60Private_t, pdrv);
    void    *payload     = NULL;
    uint32_t payload_len = 0;
    int      retval      = 0;
    if (_priv->list_opt.m_payload == NULL)
    {
        return 0;
    }
    _priv->list_opt.m_payload(pbuf, &payload, &payload_len);
    if (payload == NULL || payload_len == 0)
    {
        return 0;
    }
    retval = _bENC28J60PacketSend(pdrv, payload, payload_len);
    return retval;
}

static int _bENC28J60Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bENC28J60Private_t, pdrv);
    void    *p        = NULL;
    void   **real_buf = (void **)pbuf;
    uint32_t rlen     = 0;
    if (real_buf == NULL)
    {
        return 0;
    }
    _bENC28J60PacketRead(pdrv, &p, &rlen);
    if (p != NULL)
    {
        *real_buf = p;
    }
    return rlen;
}

static int _bENC28J60Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bENC28J60Private_t, pdrv);
    switch (cmd)
    {
        case bCMD_GET_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            bMacAddress_t *paddr = (bMacAddress_t *)param;
            memcpy(paddr->address, _priv->mac_addr, sizeof(_priv->mac_addr));
        }
        break;
        case bCMD_SET_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            bMacAddress_t *paddr = (bMacAddress_t *)param;
            _bENC28J60WriteReg(pdrv, MAADR0, paddr->address[5]);
            _bENC28J60WriteReg(pdrv, MAADR1, paddr->address[4]);
            _bENC28J60WriteReg(pdrv, MAADR2, paddr->address[3]);
            _bENC28J60WriteReg(pdrv, MAADR3, paddr->address[2]);
            _bENC28J60WriteReg(pdrv, MAADR4, paddr->address[1]);
            _bENC28J60WriteReg(pdrv, MAADR5, paddr->address[0]);

            memcpy(_priv->mac_addr, paddr->address, sizeof(_priv->mac_addr));
        }
        break;
        case bCMD_GET_LINK_STATE:
        {
            if (param == NULL)
            {
                return -1;
            }
            *((uint8_t *)param) = _priv->link_state;
        }
        break;
        case bCMD_REG_BUF_LIST:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(&_priv->list_opt, param, sizeof(bHalBufList_t));
        }
        break;
        case bCMD_REG_LINK_CALLBACK:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(&_priv->link_cb, param, sizeof(bLinkStateCb_t));
        }
        break;
    }
    return 0;
}

static int _bENC28J60Open(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    return 0;
}

static int _bENC28J60Close(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    return 0;
}

PT_THREAD(_bENC28J60LinkTask)(struct pt *pt, void *arg)
{
    static uint8_t      print_count = 0;
    uint8_t             link_state  = 0;
    bDriverInterface_t *pdrv        = (bDriverInterface_t *)arg;
    bDRIVER_GET_PRIVATE(_priv, bENC28J60Private_t, pdrv);
    B_TASK_INIT_BEGIN();
    B_TASK_INIT_END();
    PT_BEGIN(pt);
    while (1)
    {
        link_state = _bENC28J60CheckLinkUp(pdrv);
        if (link_state != _priv->link_state)
        {
            _priv->link_state = link_state;
            if (_priv->link_cb.cb)
            {
                _priv->link_cb.cb(link_state, _priv->link_cb.arg);
            }
        }
        bTaskDelayMs(pt, 500);
        print_count += 1;
        if (print_count >= 60)
        {
            _bENC28J60PrintStatus(pdrv);
            print_count = 0;
        }
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup ENC28J60_Exported_Functions
 * \{
 */
int bENC28J60_Init(bDriverInterface_t *pdrv)
{
#define RESET_READY_TIMEOUT (250)
    uint8_t timeout = 0;
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bENC28J60_Init);
    pdrv->read                              = _bENC28J60Read;
    pdrv->write                             = _bENC28J60Write;
    pdrv->ctl                               = _bENC28J60Ctl;
    pdrv->open                              = _bENC28J60Open;
    pdrv->close                             = _bENC28J60Close;
    pdrv->_private._p                       = &bENC28J60RunInfo[pdrv->drv_no];
    bENC28J60RunInfo[pdrv->drv_no].cur_bank = 0xFF;
    bHalDelayUs(100);

    uint8_t tmp_id[4] = {0};
    bHalFlashReadUID(tmp_id, sizeof(tmp_id), NULL);
    bENC28J60RunInfo[pdrv->drv_no].mac_addr[0] = 0x02;
    bENC28J60RunInfo[pdrv->drv_no].mac_addr[1] = tmp_id[0] & 0x7F;
    bENC28J60RunInfo[pdrv->drv_no].mac_addr[2] = tmp_id[1];
    bENC28J60RunInfo[pdrv->drv_no].mac_addr[3] = tmp_id[2];
    bENC28J60RunInfo[pdrv->drv_no].mac_addr[4] = tmp_id[3];
    bENC28J60RunInfo[pdrv->drv_no].mac_addr[5] = 0x01;

    bDRIVER_GET_HALIF(_if, bENC28J60_HalIf_t, pdrv);
    bHalGpioWritePin(_if->reset.port, _if->reset.pin, 0);
    bHalDelayMs(10);
    bHalGpioWritePin(_if->reset.port, _if->reset.pin, 1);
    bHalDelayMs(10);

    _bENC28J60WriteOp(pdrv, ENC28J60_SOFT_RESET, 0, ENC28J60_SOFT_RESET);
    while (!(_bENC28J60ReadReg(pdrv, ESTAT) & ESTAT_CLKRDY))
    {
        bHalDelayUs(1000);
        timeout++;
        if (timeout > RESET_READY_TIMEOUT)
        {
            b_log("ENC28J60 Reset Timeout!\r\n");
            return -1;
        }
    }

    bENC28J60RunInfo[pdrv->drv_no].next_packet_ptr = RXSTART_INIT;
    _bENC28J60WriteReg16(pdrv, ERXSTL, ERXSTH, RXSTART_INIT);
    _bENC28J60WriteReg16(pdrv, ERXRDPTL, ERXRDPTH, RXSTART_INIT);
    _bENC28J60WriteReg16(pdrv, ERXNDL, ERXNDH, RXSTOP_INIT);
    _bENC28J60WriteReg16(pdrv, ETXSTL, ETXSTH, TXSTART_INIT);
    _bENC28J60WriteReg16(pdrv, ETXNDL, ETXNDH, TXSTOP_INIT);

    _bENC28J60WriteReg(pdrv, ERXFCON, ERXFCON_UCEN | ERXFCON_BCEN | ERXFCON_CRCEN);
    _bENC28J60WriteReg(pdrv, EPMM0, 0x3f);
    _bENC28J60WriteReg(pdrv, EPMM1, 0x30);
    _bENC28J60WriteReg(pdrv, EPMCSL, 0xf9);
    _bENC28J60WriteReg(pdrv, EPMCSH, 0xf7);

    _bENC28J60WriteReg(pdrv, MACON1, MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS);
    _bENC28J60WriteReg(pdrv, MACON2, 0x00);
    _bENC28J60WriteReg(pdrv, MACON3,
                       MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN | MACON3_FULDPX);
    _bENC28J60WriteReg(pdrv, MAMXFLL, MAX_FRAMELEN & 0xFF);
    _bENC28J60WriteReg(pdrv, MAMXFLH, MAX_FRAMELEN >> 8);
    _bENC28J60WriteReg(pdrv, MABBIPG, 0x15);
    _bENC28J60WriteReg(pdrv, MAIPGL, 0x12);
    _bENC28J60WriteReg(pdrv, MAIPGH, 0x0C);

    _bENC28J60WriteReg(pdrv, MAADR0, bENC28J60RunInfo[pdrv->drv_no].mac_addr[5]);
    _bENC28J60WriteReg(pdrv, MAADR1, bENC28J60RunInfo[pdrv->drv_no].mac_addr[4]);
    _bENC28J60WriteReg(pdrv, MAADR2, bENC28J60RunInfo[pdrv->drv_no].mac_addr[3]);
    _bENC28J60WriteReg(pdrv, MAADR3, bENC28J60RunInfo[pdrv->drv_no].mac_addr[2]);
    _bENC28J60WriteReg(pdrv, MAADR4, bENC28J60RunInfo[pdrv->drv_no].mac_addr[1]);
    _bENC28J60WriteReg(pdrv, MAADR5, bENC28J60RunInfo[pdrv->drv_no].mac_addr[0]);

    uint16_t phy_duplex = _bENC28J60ReadPhy(pdrv, PHSTAT1) & PHSTAT1_PFDPX;
    if (phy_duplex)
    {
        _bENC28J60WritePhy(pdrv, PHCON1, PHCON1_PDPXMD);
        _bENC28J60WriteReg(pdrv, MACON3, _bENC28J60ReadReg(pdrv, MACON3) | MACON3_FULDPX);
    }
    else
    {
        _bENC28J60WritePhy(pdrv, PHCON1, 0);
        _bENC28J60WriteReg(pdrv, MACON3, _bENC28J60ReadReg(pdrv, MACON3) & ~MACON3_FULDPX);
        _bENC28J60WriteReg(pdrv, MABBIPG, 0x12);
    }
    _bENC28J60WritePhy(pdrv, PHCON2, PHCON2_HDLDIS);

    _bENC28J60SetBank(pdrv, ECON1);
    _bENC28J60WriteOp(pdrv, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_RXEN);

    b_log("ENC28J60 Init OK, MAC: %02X:%02X:%02X:%02X:%02X:%02X, Duplex:%s\r\n",
          bENC28J60RunInfo[pdrv->drv_no].mac_addr[0], bENC28J60RunInfo[pdrv->drv_no].mac_addr[1],
          bENC28J60RunInfo[pdrv->drv_no].mac_addr[2], bENC28J60RunInfo[pdrv->drv_no].mac_addr[3],
          bENC28J60RunInfo[pdrv->drv_no].mac_addr[4], bENC28J60RunInfo[pdrv->drv_no].mac_addr[5],
          _bENC28J60GetDuplex(pdrv) ? "Full" : "Half");

    memset(&bENC28J60TaskAttr[pdrv->drv_no], 0, sizeof(bTaskAttr_t));
    bTaskCreate("link", _bENC28J60LinkTask, pdrv, &bENC28J60TaskAttr[pdrv->drv_no]);
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_ENC28J60, bENC28J60_Init);
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

/************************ Copyright (c) 2023 babyos*****END OF FILE****/
