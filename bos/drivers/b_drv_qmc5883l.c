/**
 * \file        b_drv_qmc5883l.c
 * \version     v0.0.1
 * \date        2023-11-06
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */

/* Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_qmc5883l.h"

#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \defgroup QMC5883L_Private_TypesDefinitions
 * \{
 */

/**
 * }
 */

/**
 * \defgroup QMC5883L_Private_Defines
 * \{
 */
#define DRIVER_NAME QMC5883L

#define QMC5883L_ID 0xFF

#define MAG_X_REG_L 0X00
#define MAG_X_REG_H 0X01
#define MAG_Y_REG_L 0X02
#define MAG_Y_REG_H 0X03
#define MAG_Z_REG_L 0X04
#define MAG_Z_REG_H 0X05
#define STATUS_REG 0X06
#define TEMP_L_REG 0X07
#define TEMP_H_REG 0X08
#define CONTROL_1_REG 0X09
#define CONTROL_2_REG 0X0A
#define PERIOD_REG 0X0B
#define CHIP_ID_REG 0X0D
#define MAG_DATA_LEN 6

/**
 * }
 */

/**
 * \defgroup QMC5883L_Private_Macros
 * \{
 */
#define U82U16(msb, lsb) ((((msb)&0xffff) << 8) | (((lsb)&0xffff)))

/**
 * }
 */

/**
 * \defgroup QMC5883L_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bQMC5883L_HalIf_t, DRIVER_NAME);

/**
 * }
 */

/**
 * \defgroup QMC5883L_Private_FunctionPrototypes
 * \{
 */

/**
 * }
 */

/**
 * \defgroup QMC5883L_Private_Functions
 * \{
 */
/**
 * \brief        读寄存器并且判定是否iic失败
 * \param pdrv
 * \param reg
 * \param data
 * \param len
 * \return int -1:失败,0:正常
 */
static int _bQMC5883LReadCheckRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data,
                                   uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bQMC5883L_HalIf_t, pdrv);

    if (bHalI2CMemRead(_if, reg, 1, data, len) < 0)
    {
        return -1;
    }

    return len;
}

/**
 * \brief        写寄存器后再读寄存器,看是否配置成功,若iic失败或者写后再读值不一致则返回失败
 * \param pdrv
 * \param reg
 * \param data
 * \param len
 * \return int -1:失败,0:正常
 */
static int _bQMC5883LWriteCheckRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data,
                                    uint16_t len)
{
    uint8_t read_buf[len];
    bDRIVER_GET_HALIF(_if, bQMC5883L_HalIf_t, pdrv);
    memset(read_buf, 0, sizeof(read_buf));

    if (bHalI2CMemWrite(_if, reg, 1, data, len) < 0)
    {
        return -1;
    }

    if (bHalI2CMemRead(_if, reg, 1, read_buf, len) < 0)
    {
        return -1;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        if (read_buf[i] != data[i])
        {
            return -2;
        }
    }

    return len;
}

static int _bQMC5883LGetID(bDriverInterface_t *pdrv, uint8_t *id)
{
    if (_bQMC5883LReadCheckRegs(pdrv, CHIP_ID_REG, id, 1) < 0)
    {
        return -1;
    }
    // b_log("QMC5883L id:0x%x\n", id);
    return 0;
}

static int _bQMC5883LDefaultCfg(bDriverInterface_t *pdrv)
{
    uint8_t control_1_reg_val = 0x1d;
    uint8_t period_reg_val    = 0x01;
    uint8_t cfg1_val          = 0x40;
    uint8_t cfg2_val          = 0x01;

    if (_bQMC5883LWriteCheckRegs(pdrv, PERIOD_REG, &period_reg_val, 1) < 0)
    {
        return -1;
    }

    if (_bQMC5883LWriteCheckRegs(pdrv, 0x21, &cfg2_val, 1) < 0)
    {
        return -1;
    }

    if (_bQMC5883LWriteCheckRegs(pdrv, 0x20, &cfg1_val, 1) < 0)
    {
        return -1;
    }

    if (_bQMC5883LWriteCheckRegs(pdrv, CONTROL_1_REG, &control_1_reg_val, 1) < 0)
    {
        return -1;
    }

    return 0;
}

static int _bQMC5883LRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    uint8_t            mag_data[6];
    bQMC5883L_3Axis_t *ptmp = (bQMC5883L_3Axis_t *)pbuf;

    if (len < sizeof(bQMC5883L_3Axis_t))
    {
        return -1;
    }

    if (_bQMC5883LReadCheckRegs(pdrv, MAG_X_REG_L, &mag_data[0], MAG_DATA_LEN) < 0)
    {
        return -1;
    }

    // _bQMC5883LReadCheckRegs(pdrv, MAG_X_REG_L, &mag_data[0], 1);
    // _bQMC5883LReadCheckRegs(pdrv, MAG_X_REG_H, &mag_data[1], 1);
    // _bQMC5883LReadCheckRegs(pdrv, MAG_Y_REG_L, &mag_data[2], 1);
    // _bQMC5883LReadCheckRegs(pdrv, MAG_Y_REG_H, &mag_data[3], 1);
    // _bQMC5883LReadCheckRegs(pdrv, MAG_Z_REG_L, &mag_data[4], 1);
    // _bQMC5883LReadCheckRegs(pdrv, MAG_Z_REG_H, &mag_data[5], 1);
    // b_log("mag_reg_dat:0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", mag_data[0], mag_data[1],
    //       mag_data[2], mag_data[3], mag_data[4], mag_data[5]);
    ptmp->mag_arr[0] = (float)((short)(U82U16(mag_data[1], mag_data[0]))) / 30.0f;
    ptmp->mag_arr[1] = (float)((short)(U82U16(mag_data[3], mag_data[2]))) / 30.0f;
    ptmp->mag_arr[2] = (float)((short)(U82U16(mag_data[5], mag_data[4]))) / 30.0f;
    // b_log("mag_dat:%f %f %f\n", ptmp->mag_arr[0], ptmp->mag_arr[1], ptmp->mag_arr[2]);

    return 0;
}

/**
 * \brief
 * \param pdrv
 * \param cmd
 * \param param
 * \return int
 * -1代表期间iic错误
 * 0在bCMD_QMC5883L_WHETHER_NEWDATA_READY下表示有数据
 * 1在bCMD_QMC5883L_WHETHER_NEWDATA_READY下表示无数据
 */
static int _bQMC5883LCtl(struct bDriverIf *pdrv, uint8_t cmd, void *param)
{
    int     retval   = -1;
    uint8_t read_dat = 0;
    switch (cmd)
    {
        case bCMD_QMC5883L_WHETHER_NEWDATA_READY:
        {
            if (_bQMC5883LReadCheckRegs(pdrv, STATUS_REG, &read_dat, 1) < 0)
            {
                retval = -1;
            }
            else
            {
                if (read_dat & 0x01)
                {
                    retval = QMC5883L_NEWDATA_READY;
                }
                else
                {
                    retval = QMC5883L_NEWDATA_NOT_READY;
                }
            }
        }
        break;

        case bCMD_QMC5883L_SET_STATUS_ERR:
        {
            pdrv->status = -1;
            retval       = 0;
        }
        break;

        default:
            retval = -1;
            break;
    }
    return retval;
}
/**
 * }
 */

/**
 * \defgroup QMC5883L_Exported_Functions
 * \{
 */
// int bQMC5883L_Init_0(bDriverInterface_t *pdrv)
// {
//     _bQMC5883LSOFTRESET(pdrv);
//     bHalDelayMs(50);
//     return 0;
// }

int bQMC5883L_Init(bDriverInterface_t *pdrv)
{
    uint8_t id = 0;
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bQMC5883L_Init);
    pdrv->read = _bQMC5883LRead;
    pdrv->ctl  = _bQMC5883LCtl;

    if (_bQMC5883LGetID(pdrv, &id) < 0)
    {
        return -1;
    }

    if (id != QMC5883L_ID)
    {
        return -1;
    }

    if (_bQMC5883LDefaultCfg(pdrv) < 0)
    {
        return -1;
    }

    return 0;
}

// bDRIVER_REG_INIT_0(B_DRIVER_QMC5883L, bQMC5883L_Init_0);
#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_QMC5883L, bQMC5883L_Init);
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

/***** Copyright (c) 2023 miniminiminini *****END OF FILE*****/
