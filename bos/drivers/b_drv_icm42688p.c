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

/**
 * \defgroup ICM42688P_Private_TypesDefinitions
 * \{
 */

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Defines
 * \{
 */
#define DRIVER_NAME ICM42688P

#define POWER_MGMT 0x4E
#define DEVICE_CONFIG 0x11
#define DRIVE_CONFIG 0x13
#define WHO_AM_I 0x75
#define TEMP_DATA1 0x1D
#define TEMP_DATA0 0x1E
#define GYRO_CONFIG0 0x4F
#define ACCEL_CONFIG0 0x50
#define GYRO_CONFIG1 0x51
#define GYRO_ACCEL_CONFIG0 0x52
#define ACCEL_CONFIG1 0x53
#define XA_ST_DATA 0x3B
#define YA_ST_DATA 0x3C
#define ZA_ST_DATA 0x3D
#define BANK_SEL 0x76
#define ACCEL_DATA_X1 0x1F
#define ACCEL_DATA_X0 0x20
#define ACCEL_DATA_Y1 0x21
#define ACCEL_DATA_Y0 0x22
#define ACCEL_DATA_Z1 0x23
#define ACCEL_DATA_Z0 0x24
#define GYRO_DATA_X1 0x25
#define GYRO_DATA_X0 0x26
#define GYRO_DATA_Y1 0x27
#define GYRO_DATA_Y0 0x28
#define GYRO_DATA_Z1 0x29
#define GYRO_DATA_Z0 0x2A

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
 * }
 */

/**
 * \defgroup ICM42688P_Exported_Functions
 * \{
 */
int bICM20948_Init(bDriverInterface_t *pdrv)
{
    uint8_t w_data = 0;
    uint8_t r_data = 0;
    uint8_t data[8];

    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bICM20948_Init);
    pdrv->read = _bICM20948Read;

    if (ICM20948GetID(pdrv, &r_data) < 0)
    {
        return -1;
    }
    if (r_data != ICM20948_ID)
    {
        return -1;
    }

    ICM20948_Mag_Reset();
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3);
    /* Reset I2C master clock. */
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_MST_CTRL, 0);

    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);
    bICM20948ReadRegs(pdrv, 0x03, &r_data, 1);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_USER_CTRL, r_data | REG_VAL_BIT_I2C_MST_EN);

    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_MST_CTRL, REG_ADD_I2C_MST_CTRL_CLK_400KHZ);
    F_Delay_ms(10);

    // configure gyro
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_2);
    F_IIC_WriteByte(
        I2C_ADD_ICM20948, REG_ADD_GYRO_CONFIG_1,
        REG_VAL_BIT_GYRO_DLPCFG_6 | REG_VAL_BIT_GYRO_FS_2000DPS | REG_VAL_BIT_GYRO_DLPF);

    // configure acc
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_SMPLRT_DIV_2, 0x00);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_CONFIG,
                    REG_VAL_BIT_ACCEL_DLPCFG_6 | REG_VAL_BIT_ACCEL_FS_4g | REG_VAL_BIT_ACCEL_DLPF);

    // checkMag
    uint8_t ret[2];
    ICM20948_Mag_Read(I2C_ADD_ICM20948_AK09916, REG_ADD_MAG_WIA1, 2, ret);
    if (!((ret[0] == REG_VAL_MAG_WIA1) && (ret[1] == REG_VAL_MAG_WIA2)))
    {
        return -2;
    }
    F_Delay_ms(10);
    ICM20948_Mag_Write(I2C_ADD_ICM20948_AK09916, REG_ADD_MAG_CNTL2, REG_VAL_MAG_MODE_100HZ);
    F_Delay_ms(10);
    ICM20948_Mag_Read(I2C_ADD_ICM20948_AK09916, REG_ADD_MAG_DATA, 8, data);

    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);

    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_ICM20948, bICM20948_Init);
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
