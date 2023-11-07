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

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Defines
 * \{
 */
#define DRIVER_NAME ICM42688P

#define ICM42688Q_ID 0x47
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
#define FIFO_CONFIG_INIT 0x16
#define FIFO_CONFIGURATION 0x5F
#define FIFO_DATA_REG 0x30
#define FIFO_DATA_LEN 16

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Macros
 * \{
 */
#define U82U16(a, b) ((((a)&0xffff) << 8) | (((b)&0xffff) >> 8))

/**
 * }
 */

/**
 * \defgroup ICM42688P_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bICM42688P_HalIf_t, DRIVER_NAME);

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
static int _bICM42688PReadRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data, uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bICM42688P_HalIf_t, pdrv);

    bHalI2CMemRead(_if, reg, 1, data, len);
    return 0;
}

static int _bICM42688PWriteRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data, uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bICM42688P_HalIf_t, pdrv);

    bHalI2CMemWrite(_if, reg, 1, data, len);
    return 0;
}

static uint8_t _bICM42688PGetID(bDriverInterface_t *pdrv)
{
    uint8_t id = 0;
    _bICM42688PReadRegs(pdrv, WHO_AM_I, &id, 1);
    // b_log("ICM42688P id:0x%x\n", id);
    return id;
}

static void _bICM42688PDefaultCfg(bDriverInterface_t *pdrv)
{
    uint8_t device_config_val      = 0x01;
    uint8_t power_mgmt_val         = 0x1f;
    uint8_t fifo_config_init_val   = 0x40;
    uint8_t fifo_configuration_val = 0x03;
    bHalDelayMs(10);
    _bICM42688PWriteRegs(pdrv, DEVICE_CONFIG, &device_config_val, 1);
    bHalDelayMs(60);
    _bICM42688PWriteRegs(pdrv, POWER_MGMT, &power_mgmt_val, 1);
    bHalDelayMs(25);
    _bICM42688PWriteRegs(pdrv, FIFO_CONFIG_INIT, &fifo_config_init_val, 1);
    _bICM42688PWriteRegs(pdrv, FIFO_CONFIGURATION, &fifo_configuration_val, 1);
    bHalDelayMs(10);
}

static int _bICM42688PRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    uint8_t             fifo_data[16];
    bICM42688P_6Axis_t *ptmp = (bICM42688P_6Axis_t *)pbuf;

    if (len < sizeof(bICM42688P_6Axis_t))
    {
        return -1;
    }
    _bICM42688PReadRegs(pdrv, FIFO_DATA_REG, fifo_data, FIFO_DATA_LEN);
    ptmp->acc_arr[0]  = U82U16(fifo_data[1], fifo_data[2]);
    ptmp->acc_arr[1]  = U82U16(fifo_data[3], fifo_data[4]);
    ptmp->acc_arr[2]  = U82U16(fifo_data[5], fifo_data[6]);
    ptmp->gyro_arr[0] = U82U16(fifo_data[7], fifo_data[8]);
    ptmp->gyro_arr[1] = U82U16(fifo_data[9], fifo_data[10]);
    ptmp->gyro_arr[2] = U82U16(fifo_data[11], fifo_data[12]);
    // b_log("acc_dat:%d %d%d \n", ptmp->acc_arr[0], ptmp->acc_arr[1], ptmp->acc_arr[2]);
    // b_log("gyro_dat:%d %d%d \n", ptmp->gyro_arr[0], ptmp->gyro_arr[1], ptmp->gyro_arr[2]);

    return 0;
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
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bICM42688P_Init);
    pdrv->read = _bICM42688PRead;
    _bICM42688PDefaultCfg(pdrv);
    if (_bICM42688PGetID(pdrv) != ICM42688Q_ID)
    {
        return -1;
    }

    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_ICM42688P, bICM42688P_Init);
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
