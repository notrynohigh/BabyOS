/**
 * \file        b_drv_qmi8658a.c
 * \version     v0.0.1
 * \date        2023-11-09
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */

/* Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_qmi8658a.h"

#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \defgroup QMI8658A_Private_TypesDefinitions
 * \{
 */

/**
 * }
 */

/**
 * \defgroup QMI8658A_Private_Defines
 * \{
 */
#define DRIVER_NAME QMI8658A
#define QMI8658A_ID 0X05
///< Configuration Registers>
#define address 0X6B   // device address
#define WHO_AM_I 0X00  // Device identifier
#define CTRL1 0x02     // Serial Interface and Sensor Enable
#define CTRL2 0x03     // Accelerometer Settings
#define CTRL3 0x04     // Gyroscope Settings
#define CTRL4 0X05     // Magnetometer Settings
#define CTRL5 0X06     // Sensor Data Processing Settings
#define CTRL7 0x08     // Enable Sensors and Configure Data Reads
#define CTRL8 0X09     // Reserved – Special Settings

///< Sensor Data Output Registers>
#define AccX_L 0x35
#define AccX_H 0x36
#define AccY_L 0x37
#define AccY_H 0x38
#define AccZ_L 0x39
#define AccZ_H 0x3A
#define TEMP_L 0x33

#define GyrX_L 0x3B
#define GyrX_H 0x3C
#define GyrY_L 0x3D
#define GyrY_H 0x3E
#define GyrZ_L 0x3F
#define GyrZ_H 0x40

#define RESET_REG 0x60

/**
 * }
 */

/**
 * \defgroup QMI8658A_Private_Macros
 * \{
 */
#define U8ToFloat(msb, lsb) ((float)((short)((((msb)&0xffff) << 8) | (((lsb)&0xffff)))))

/**
 * }
 */

/**
 * \defgroup QMI8658A_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bQMI8658A_HalIf_t, DRIVER_NAME);

/**
 * }
 */

/**
 * \defgroup QMI8658A_Private_FunctionPrototypes
 * \{
 */

/**
 * \brief        判定iic是否失败
 * \param pdrv
 * \param reg
 * \param data
 * \param len
 * \return int
 */
static int _bQMI8658AReadCheckRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data,
                                   uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bQMI8658A_HalIf_t, pdrv);

    if (bHalI2CMemRead(_if, reg, 1, data, len) < 0)
    {
        return -1;
    }

    return len;
}

static int _bQMI8658AWriteCheckRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data,
                                    uint16_t len)
{
    uint8_t read_buf[len];
    bDRIVER_GET_HALIF(_if, bQMI8658A_HalIf_t, pdrv);
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

static void _bQMI8658ASOFTRESET(bDriverInterface_t *pdrv)
{
    uint8_t reset_reg_val = 0x0B;
    _bQMI8658AWriteCheckRegs(pdrv, RESET_REG, &reset_reg_val, 1);
    bHalDelayMs(50);
}

static int _bQMI8658AGetID(bDriverInterface_t *pdrv, uint8_t *id)
{
    if (_bQMI8658AReadCheckRegs(pdrv, WHO_AM_I, id, 1) < 0)
    {
        return -1;
    }
    // b_log("QMI8658A id:0x%x\n", id);
    return 0;
}

static int _bQMI8658ADefaultCfg(bDriverInterface_t *pdrv)
{
    uint8_t ctrl1_val =
        0x40;  // Serial Interface and Sensor Enable<串行接口（SPI或I 2 C）地址自动递增>
    uint8_t ctrl7_val =
        0x03;  // Enable Sensors and Configure Data Reads<Enable Gyroscope Accelerometer>
    uint8_t ctrl2_val = 0x34;  // Accelerometer Settings<±16g  500Hz>
    uint8_t ctrl3_val = 0x64;  // Gyroscope Settings< ±2048dps 500Hz>
    uint8_t ctrl5_val =
        0x11;  // Sensor Data Processing Settings<Enable Gyroscope Accelerometer 低通滤波>

    if (_bQMI8658AWriteCheckRegs(pdrv, CTRL1, &ctrl1_val, 1) < 0)
    {
        return -1;
    }
    if (_bQMI8658AWriteCheckRegs(pdrv, CTRL7, &ctrl7_val, 1) < 0)
    {
        return -1;
    }
    if (_bQMI8658AWriteCheckRegs(pdrv, CTRL2, &ctrl2_val, 1) < 0)
    {
        return -1;
    }
    if (_bQMI8658AWriteCheckRegs(pdrv, CTRL3, &ctrl3_val, 1) < 0)
    {
        return -1;
    }
    if (_bQMI8658AWriteCheckRegs(pdrv, CTRL5, &ctrl5_val, 1) < 0)
    {
        return -1;
    }

    return 0;
}

static int _bQMI8658ARead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    uint8_t            axis6_data[12];
    bQMI8658A_6Axis_t *ptmp = (bQMI8658A_6Axis_t *)pbuf;

    if (len < sizeof(bQMI8658A_6Axis_t))
    {
        return -1;
    }

    if (_bQMI8658AReadCheckRegs(pdrv, AccX_L, &axis6_data[0], 6) < 0)
    {
        return -1;
    }

    if (_bQMI8658AReadCheckRegs(pdrv, GyrX_L, &axis6_data[6], 6) < 0)
    {
        return -1;
    }

    ptmp->acc_arr[0]  = U8ToFloat(axis6_data[1], axis6_data[0]) * 16 * 9.8f / 65536;
    ptmp->acc_arr[1]  = U8ToFloat(axis6_data[3], axis6_data[2]) * 16 * 9.8f / 65536;
    ptmp->acc_arr[2]  = U8ToFloat(axis6_data[5], axis6_data[4]) * 16 * 9.8f / 65536;
    ptmp->gyro_arr[0] = U8ToFloat(axis6_data[7], axis6_data[6]) * 2048 / 65536 / 57.3f;
    ptmp->gyro_arr[1] = U8ToFloat(axis6_data[9], axis6_data[8]) * 2048 / 65536 / 57.3f;
    ptmp->gyro_arr[2] = U8ToFloat(axis6_data[11], axis6_data[10]) * 2048 / 65536 / 57.3f;
    // b_log("acc_dat:0x%02x 0x%02x 0x%02x\n", ptmp->acc_arr[0], ptmp->acc_arr[1], ptmp->acc_arr[2]);
    // b_log("gyro_dat:0x%02x 0x%02x 0x%02x\n", ptmp->gyro_arr[0], ptmp->gyro_arr[1],
    //       ptmp->gyro_arr[2]);

    return 0;
}

static int _bQMI8658ACtl(struct bDriverIf *pdrv, uint8_t cmd, void *param)
{
    int retval = 0;
    switch (cmd)
    {
        case bCMD_QMI8658A_SET_STATUS_ERR:
        {
            pdrv->status = -1;
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
 * \defgroup QMI8658A_Private_Functions
 * \{
 */

/**
 * }
 */

/**
 * \defgroup QMI8658A_Exported_Functions
 * \{
 */
int bQMI8658A_Init(bDriverInterface_t *pdrv)
{
    uint8_t id = 0;
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bQMI8658A_Init);
    pdrv->read = _bQMI8658ARead;
    pdrv->ctl  = _bQMI8658ACtl;

    if (_bQMI8658AGetID(pdrv, &id) < 0)
    {
        return -1;
    }

    if (id != QMI8658A_ID)
    {
        return -1;
    }

    _bQMI8658ASOFTRESET(pdrv);

    if (_bQMI8658ADefaultCfg(pdrv) < 0)
    {
        return -1;
    }

    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_QMI8658A, bQMI8658A_Init);
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
