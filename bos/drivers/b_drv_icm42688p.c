/**
 * \file        b_drv_icm42688p.c
 * \version     v0.0.1
 * \date        2023-11-01
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */

/* Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_icm42688p.h"

#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \defgroup ICM42688P_Private_TypesDefinitions
 * \{
 */
typedef enum
{
    // Accesible from all user banks
    REG_BANK_SEL = 0x76,

    // User Bank 0
    UB0_REG_DEVICE_CONFIG = 0x11,
    // break
    UB0_REG_DRIVE_CONFIG = 0x13,
    UB0_REG_INT_CONFIG   = 0x14,
    // break
    UB0_REG_FIFO_CONFIG = 0x16,
    // break
    UB0_REG_TEMP_DATA1    = 0x1D,
    UB0_REG_TEMP_DATA0    = 0x1E,
    UB0_REG_ACCEL_DATA_X1 = 0x1F,
    UB0_REG_ACCEL_DATA_X0 = 0x20,
    UB0_REG_ACCEL_DATA_Y1 = 0x21,
    UB0_REG_ACCEL_DATA_Y0 = 0x22,
    UB0_REG_ACCEL_DATA_Z1 = 0x23,
    UB0_REG_ACCEL_DATA_Z0 = 0x24,
    UB0_REG_GYRO_DATA_X1  = 0x25,
    UB0_REG_GYRO_DATA_X0  = 0x26,
    UB0_REG_GYRO_DATA_Y1  = 0x27,
    UB0_REG_GYRO_DATA_Y0  = 0x28,
    UB0_REG_GYRO_DATA_Z1  = 0x29,
    UB0_REG_GYRO_DATA_Z0  = 0x2A,
    UB0_REG_TMST_FSYNCH   = 0x2B,
    UB0_REG_TMST_FSYNCL   = 0x2C,
    UB0_REG_INT_STATUS    = 0x2D,
    UB0_REG_FIFO_COUNTH   = 0x2E,
    UB0_REG_FIFO_COUNTL   = 0x2F,
    UB0_REG_FIFO_DATA     = 0x30,
    UB0_REG_APEX_DATA0    = 0x31,
    UB0_REG_APEX_DATA1    = 0x32,
    UB0_REG_APEX_DATA2    = 0x33,
    UB0_REG_APEX_DATA3    = 0x34,
    UB0_REG_APEX_DATA4    = 0x35,
    UB0_REG_APEX_DATA5    = 0x36,
    UB0_REG_INT_STATUS2   = 0x37,
    UB0_REG_INT_STATUS3   = 0x38,
    // break
    UB0_REG_SIGNAL_PATH_RESET  = 0x4B,
    UB0_REG_INTF_CONFIG0       = 0x4C,
    UB0_REG_INTF_CONFIG1       = 0x4D,
    UB0_REG_PWR_MGMT0          = 0x4E,
    UB0_REG_GYRO_CONFIG0       = 0x4F,
    UB0_REG_ACCEL_CONFIG0      = 0x50,
    UB0_REG_GYRO_CONFIG1       = 0x51,
    UB0_REG_GYRO_ACCEL_CONFIG0 = 0x52,
    UB0_REG_ACCEFL_CONFIG1     = 0x53,
    UB0_REG_TMST_CONFIG        = 0x54,
    // break
    UB0_REG_APEX_CONFIG0 = 0x56,
    UB0_REG_SMD_CONFIG   = 0x57,
    // break
    UB0_REG_FIFO_CONFIG1 = 0x5F,
    UB0_REG_FIFO_CONFIG2 = 0x60,
    UB0_REG_FIFO_CONFIG3 = 0x61,
    UB0_REG_FSYNC_CONFIG = 0x62,
    UB0_REG_INT_CONFIG0  = 0x63,
    UB0_REG_INT_CONFIG1  = 0x64,
    UB0_REG_INT_SOURCE0  = 0x65,
    UB0_REG_INT_SOURCE1  = 0x66,
    // break
    UB0_REG_INT_SOURCE3 = 0x68,
    UB0_REG_INT_SOURCE4 = 0x69,
    // break
    UB0_REG_FIFO_LOST_PKT0 = 0x6C,
    UB0_REG_FIFO_LOST_PKT1 = 0x6D,
    // break
    UB0_REG_SELF_TEST_CONFIG = 0x70,
    // break
    UB0_REG_WHO_AM_I = 0x75,

    // User Bank 1
    UB1_REG_SENSOR_CONFIG0 = 0x03,
    // break
    UB1_REG_GYRO_CONFIG_STATIC2  = 0x0B,
    UB1_REG_GYRO_CONFIG_STATIC3  = 0x0C,
    UB1_REG_GYRO_CONFIG_STATIC4  = 0x0D,
    UB1_REG_GYRO_CONFIG_STATIC5  = 0x0E,
    UB1_REG_GYRO_CONFIG_STATIC6  = 0x0F,
    UB1_REG_GYRO_CONFIG_STATIC7  = 0x10,
    UB1_REG_GYRO_CONFIG_STATIC8  = 0x11,
    UB1_REG_GYRO_CONFIG_STATIC9  = 0x12,
    UB1_REG_GYRO_CONFIG_STATIC10 = 0x13,
    // break
    UB1_REG_XG_ST_DATA = 0x5F,
    UB1_REG_YG_ST_DATA = 0x60,
    UB1_REG_ZG_ST_DATA = 0x61,
    UB1_REG_TMSTVAL0   = 0x62,
    UB1_REG_TMSTVAL1   = 0x63,
    UB1_REG_TMSTVAL2   = 0x64,
    // break
    UB1_REG_INTF_CONFIG4 = 0x7A,
    UB1_REG_INTF_CONFIG5 = 0x7B,
    UB1_REG_INTF_CONFIG6 = 0x7C,

    // User Bank 2
    UB2_REG_ACCEL_CONFIG_STATIC2 = 0x03,
    UB2_REG_ACCEL_CONFIG_STATIC3 = 0x04,
    UB2_REG_ACCEL_CONFIG_STATIC4 = 0x05,
    // break
    UB2_REG_XA_ST_DATA = 0x3B,
    UB2_REG_YA_ST_DATA = 0x3C,
    UB2_REG_ZA_ST_DATA = 0x3D,

    // User Bank 4
    UB4_REG_APEX_CONFIG1 = 0x40,
    UB4_REG_APEX_CONFIG2 = 0x41,
    UB4_REG_APEX_CONFIG3 = 0x42,
    UB4_REG_APEX_CONFIG4 = 0x43,
    UB4_REG_APEX_CONFIG5 = 0x44,
    UB4_REG_APEX_CONFIG6 = 0x45,
    UB4_REG_APEX_CONFIG7 = 0x46,
    UB4_REG_APEX_CONFIG8 = 0x47,
    UB4_REG_APEX_CONFIG9 = 0x48,
    // break
    UB4_REG_ACCEL_WOM_X_THR = 0x4A,
    UB4_REG_ACCEL_WOM_Y_THR = 0x4B,
    UB4_REG_ACCEL_WOM_Z_THR = 0x4C,
    UB4_REG_INT_SOURCE6     = 0x4D,
    UB4_REG_INT_SOURCE7     = 0x4E,
    UB4_REG_INT_SOURCE8     = 0x4F,
    UB4_REG_INT_SOURCE9     = 0x50,
    UB4_REG_INT_SOURCE10    = 0x51,
    // break
    UB4_REG_OFFSET_USER0 = 0x77,
    UB4_REG_OFFSET_USER1 = 0x78,
    UB4_REG_OFFSET_USER2 = 0x79,
    UB4_REG_OFFSET_USER3 = 0x7A,
    UB4_REG_OFFSET_USER4 = 0x7B,
    UB4_REG_OFFSET_USER5 = 0x7C,
    UB4_REG_OFFSET_USER6 = 0x7D,
    UB4_REG_OFFSET_USER7 = 0x7E,
    UB4_REG_OFFSET_USER8 = 0x7F,
} Enum_ICM42688reg;

typedef enum
{
    odr32k    = 0x01,  // LN mode only
    odr16k    = 0x02,  // LN mode only
    odr8k     = 0x03,  // LN mode only
    odr4k     = 0x04,  // LN mode only
    odr2k     = 0x05,  // LN mode only
    odr1k     = 0x06,  // LN mode only
    odr200    = 0x07,
    odr100    = 0x08,
    odr50     = 0x09,
    odr25     = 0x0A,
    odr12_5   = 0x0B,
    odr6a25   = 0x0C,  // LP mode only (accel only)
    odr3a125  = 0x0D,  // LP mode only (accel only)
    odr1a5625 = 0x0E,  // LP mode only (accel only)
    odr500    = 0x0F,
} Enum_ODR;
typedef enum
{
    dps2000   = 0x00,
    dps1000   = 0x01,
    dps500    = 0x02,
    dps250    = 0x03,
    dps125    = 0x04,
    dps62_5   = 0x05,
    dps31_25  = 0x06,
    dps15_625 = 0x07
} Enum_GyroFS;
typedef enum
{
    gpm16 = 0x00,
    gpm8  = 0x01,
    gpm4  = 0x02,
    gpm2  = 0x03
} Enum_AccelFS;

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Defines
 * \{
 */
#define DRIVER_NAME ICM42688P

#define ICM42688P_ID 0x47

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Macros
 * \{
 */

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bICM42688P_HalIf_t, DRIVER_NAME);

///\brief Full scale resolution factors
// static float _accelScale = 0.0f;
// static float _gyroScale  = 0.0f;

///\brief Full scale selections
// static Enum_AccelFS _accelFS;
// static Enum_GyroFS  _gyroFS;

///\brief Conversion formula to get temperature in Celsius (Sec 4.13)
static float TEMP_DATA_REG_SCALE = 132.48f;
static float TEMP_OFFSET         = 25.0f;

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_FunctionPrototypes
 * \{
 */

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Functions
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
static int _bICM42688PReadCheckRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data,
                                    uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bICM42688P_HalIf_t, pdrv);

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
static int _bICM42688PWriteCheckRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data,
                                     uint16_t len)
{
    uint8_t read_buf[len];
    bDRIVER_GET_HALIF(_if, bICM42688P_HalIf_t, pdrv);
    memset(read_buf, 0, sizeof(read_buf));

    if (bHalI2CMemWrite(_if, reg, 1, data, len) < 0)
    {
        return -1;
    }

    bHalDelayMs(10);

    if (bHalI2CMemRead(_if, reg, 1, read_buf, len) < 0)
    {
        return -2;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        if (read_buf[i] != data[i])
        {
            return -3;
        }
    }

    return len;
}

// static int _bICM42688PSetBank(bDriverInterface_t *pdrv, uint8_t bank)
// {
//     if (_bICM42688PWriteCheckRegs(pdrv, REG_BANK_SEL, &bank, 1) < 0)
//     {
//         return -1;
//     }

//     return 0;
// }

static int _bICM42688PSoftReset(bDriverInterface_t *pdrv)
{
    uint8_t ub0_reg_device_config_val = 0x01;

    // if (_bICM42688PSetBank(pdrv, 0) < 0)
    // {
    //     return -1;
    // }

    _bICM42688PWriteCheckRegs(pdrv, UB0_REG_DEVICE_CONFIG, &ub0_reg_device_config_val, 1);

        return 0;
}

static int _bICM42688PGetID(bDriverInterface_t *pdrv, uint8_t *id)
{
   // if (_bICM42688PSetBank(pdrv, 0) < 0)
   // {
   //     return -1;
   // }

   if (_bICM42688PReadCheckRegs(pdrv, UB0_REG_WHO_AM_I, id, 1) < 0)
   {
        return -1;
    }

    // b_log("ICM42688P id:0x%x\n", id);

    return 0;
}

static int _bICM42688PSetAccelFS(bDriverInterface_t *pdrv, Enum_AccelFS fssel)
{
    uint8_t read_val = 0;

    // if (_bICM42688PSetBank(pdrv, 0) < 0)
    // {
    //     return -1;
    // }

    if (_bICM42688PReadCheckRegs(pdrv, UB0_REG_ACCEL_CONFIG0, &read_val, 1) < 0)
    {
        return -1;
    }

    // only change FS_SEL in reg
    read_val = (fssel << 5) | (read_val & 0x1F);

    if (_bICM42688PWriteCheckRegs(pdrv, UB0_REG_ACCEL_CONFIG0, &read_val, 1) < 0)
    {
        return -1;
    }

    // _accelScale = (float)(1 << (4 - fssel)) / 32768.0f;
    // _accelFS    = fssel;

    return 0;
}

static int _bICM42688PSetGyroFS(bDriverInterface_t *pdrv, Enum_GyroFS fssel)
{
    uint8_t read_val = 0;

    // if (_bICM42688PSetBank(pdrv, 0) < 0)
    // {
    //     return -1;
    // }

    if (_bICM42688PReadCheckRegs(pdrv, UB0_REG_GYRO_CONFIG0, &read_val, 1) < 0)
    {
        return -1;
    }

    // only change FS_SEL in reg
    read_val = (fssel << 5) | (read_val & 0x1F);

    if (_bICM42688PWriteCheckRegs(pdrv, UB0_REG_GYRO_CONFIG0, &read_val, 1) < 0)
    {
        return -1;
    }

    // _gyroScale = (2000.0f / (float)(1 << fssel)) / 32768.0f;
    // _gyroFS    = fssel;

    return 0;
}

static int _bICM42688PRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    uint8_t             i = 0;
    uint8_t             _buffer[16];
    int16_t             _rawMeas[7];  // temp, accel xyz, gyro xyz
    bICM42688P_6Axis_t *ptmp = (bICM42688P_6Axis_t *)pbuf;

    if (_bICM42688PReadCheckRegs(pdrv, UB0_REG_TEMP_DATA1, _buffer, 14) < 0)
    {
        return -1;
    }

    for (i = 0; i < 7; i++)
    {
        _rawMeas[i] = ((int16_t)_buffer[i * 2] << 8) | _buffer[i * 2 + 1];
    }

    ptmp->temperature = ((float)(_rawMeas[0]) / TEMP_DATA_REG_SCALE) + TEMP_OFFSET;

    ptmp->acc_arr[0] = _rawMeas[1];
    ptmp->acc_arr[1] = _rawMeas[2];
    ptmp->acc_arr[2] = _rawMeas[3];

    ptmp->gyro_arr[0] = _rawMeas[4];
    ptmp->gyro_arr[1] = _rawMeas[5];
    ptmp->gyro_arr[2] = _rawMeas[6];

    return 0;
}

static int _bICM42688PCtl(struct bDriverIf *pdrv, uint8_t cmd, void *param)
{
    int retval = 0;
    switch (cmd)
    {
        case bCMD_ICM42688P_SET_STATUS_ERR:
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
 * \defgroup ICM42688P_Exported_Functions
 * \{
 */
int bICM42688P_Init(bDriverInterface_t *pdrv)
{
    uint8_t id                    = 0;
    uint8_t ub0_reg_pwr_mgmt0_val = 0x0F;

    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bICM42688P_Init);
    pdrv->read = _bICM42688PRead;
    pdrv->ctl  = _bICM42688PCtl;

    if (_bICM42688PGetID(pdrv, &id) < 0)
    {
        return -1;
    }

    if (id != ICM42688P_ID)
    {
        return -1;
    }

    if (_bICM42688PSoftReset(pdrv) < 0)
    {
        return -1;
    }

    // turn on accel and gyro in Low Noise (LN) Mode
    if (_bICM42688PWriteCheckRegs(pdrv, UB0_REG_PWR_MGMT0, &ub0_reg_pwr_mgmt0_val, 1) < 0)
    {
        return -1;
    }

    // 16G is default -- do this to set up accel resolution scaling
    if (_bICM42688PSetAccelFS(pdrv, gpm16) < 0)
    {
        return -1;
    }

    // 2000DPS is default -- do this to set up gyro resolution scaling
    if (_bICM42688PSetGyroFS(pdrv, dps2000) < 0)
    {
        return -1;
    }

    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_ICM42688P, bICM42688P_Init);
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
