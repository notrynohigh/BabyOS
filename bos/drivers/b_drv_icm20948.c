/**
 *!
 * \file        b_drv_ICM20948.c
 * \version     v0.0.1
 * \date        2020/03/25
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
#include "drivers/inc/b_drv_icm20948.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup ICM20948
 * \{
 */

/**
 * \defgroup ICM20948_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_Defines
 * \{
 */
#define DRIVER_NAME ICM20948

/* define ICM-20948 Register */
#define ICM20948_ID 0xEAU
#define I2C_ADD_ICM20948 0xD0
#define I2C_ADD_ICM20948_AK09916 0x0C
#define I2C_ADD_ICM20948_AK09916_READ 0x80
/* user bank 0 register */
#define REG_ADD_WHO_AM_I 0x00
#define REG_VAL_WIA 0xEA
#define REG_ADD_USER_CTRL 0x03
#define REG_VAL_BIT_DMP_EN 0x80
#define REG_VAL_BIT_FIFO_EN 0x40
#define REG_VAL_BIT_I2C_MST_EN 0x20
#define REG_VAL_BIT_I2C_IF_DIS 0x10
#define REG_VAL_BIT_DMP_RST 0x08
#define REG_VAL_BIT_DIAMOND_DMP_RST 0x04
#define REG_ADD_PWR_MIGMT_1 0x06
#define REG_VAL_ALL_RGE_RESET 0x80
#define REG_VAL_RUN_MODE 0x01  // Non low-power mode
#define REG_ADD_LP_CONFIG 0x05
#define REG_ADD_PWR_MGMT_1 0x06
#define REG_ADD_PWR_MGMT_2 0x07
#define REG_VAL_MGMNT_2_SEN_ENABLE 0x00
#define REG_ADD_ACCEL_XOUT_H 0x2D
#define REG_ADD_ACCEL_XOUT_L 0x2E
#define REG_ADD_ACCEL_YOUT_H 0x2F
#define REG_ADD_ACCEL_YOUT_L 0x30
#define REG_ADD_ACCEL_ZOUT_H 0x31
#define REG_ADD_ACCEL_ZOUT_L 0x32
#define REG_ADD_GYRO_XOUT_H 0x33
#define REG_ADD_GYRO_XOUT_L 0x34
#define REG_ADD_GYRO_YOUT_H 0x35
#define REG_ADD_GYRO_YOUT_L 0x36
#define REG_ADD_GYRO_ZOUT_H 0x37
#define REG_ADD_GYRO_ZOUT_L 0x38
#define REG_ADD_EXT_SENS_DATA_00 0x3B
#define REG_ADD_REG_BANK_SEL 0x7F
#define REG_VAL_REG_BANK_0 0x00
#define REG_VAL_REG_BANK_1 0x10
#define REG_VAL_REG_BANK_2 0x20
#define REG_VAL_REG_BANK_3 0x30
/* user bank 1 register */
/* user bank 2 register */
#define REG_ADD_GYRO_SMPLRT_DIV 0x00
#define REG_ADD_GYRO_CONFIG_1 0x01
#define REG_VAL_BIT_GYRO_DLPCFG_2 0x10   /* bit[5:3] */
#define REG_VAL_BIT_GYRO_DLPCFG_4 0x20   /* bit[5:3] */
#define REG_VAL_BIT_GYRO_DLPCFG_6 0x30   /* bit[5:3] */
#define REG_VAL_BIT_GYRO_FS_250DPS 0x00  /* bit[2:1] */
#define REG_VAL_BIT_GYRO_FS_500DPS 0x02  /* bit[2:1] */
#define REG_VAL_BIT_GYRO_FS_1000DPS 0x04 /* bit[2:1] */
#define REG_VAL_BIT_GYRO_FS_2000DPS 0x06 /* bit[2:1] */
#define REG_VAL_BIT_GYRO_DLPF 0x01       /* bit[0]   */
#define REG_ADD_ACCEL_SMPLRT_DIV_2 0x11
#define REG_ADD_ACCEL_CONFIG 0x14
#define REG_VAL_BIT_ACCEL_DLPCFG_2 0x10 /* bit[5:3] */
#define REG_VAL_BIT_ACCEL_DLPCFG_4 0x20 /* bit[5:3] */
#define REG_VAL_BIT_ACCEL_DLPCFG_6 0x30 /* bit[5:3] */
#define REG_VAL_BIT_ACCEL_FS_2g 0x00    /* bit[2:1] */
#define REG_VAL_BIT_ACCEL_FS_4g 0x02    /* bit[2:1] */
#define REG_VAL_BIT_ACCEL_FS_8g 0x04    /* bit[2:1] */
#define REG_VAL_BIT_ACCEL_FS_16g 0x06   /* bit[2:1] */
#define REG_VAL_BIT_ACCEL_DLPF 0x01     /* bit[0]   */
/* user bank 3 register */
#define REG_ADD_I2C_MST_CTRL 0x01
#define REG_ADD_I2C_MST_CTRL_CLK_400KHZ \
    0x07  // Gives 345.6kHz and is recommended to achieve max 400kHz
#define REG_ADD_I2C_SLV0_ADDR 0x03
#define REG_ADD_I2C_SLV0_REG 0x04
#define REG_ADD_I2C_SLV0_CTRL 0x05
#define REG_VAL_BIT_SLV0_EN 0x80
#define REG_VAL_BIT_MASK_LEN 0x07
#define REG_ADD_I2C_SLV0_DO 0x06
#define REG_ADD_I2C_SLV1_ADDR 0x07
#define REG_ADD_I2C_SLV1_REG 0x08
#define REG_ADD_I2C_SLV1_CTRL 0x09
#define REG_ADD_I2C_SLV1_DO 0x0A
#define REG_ADD_MAG_WIA1 0x00
#define REG_VAL_MAG_WIA1 0x48
#define REG_ADD_MAG_WIA2 0x01
#define REG_VAL_MAG_WIA2 0x09
#define REG_ADD_MAG_ST2 0x10
#define REG_ADD_MAG_DATA 0x11
#define REG_ADD_MAG_CNTL2 0x31
#define REG_VAL_MAG_MODE_PD 0x00
#define REG_VAL_MAG_MODE_SM 0x01
#define REG_VAL_MAG_MODE_10HZ 0x02
#define REG_VAL_MAG_MODE_20HZ 0x04
#define REG_VAL_MAG_MODE_50HZ 0x05
#define REG_VAL_MAG_MODE_100HZ 0x08
#define REG_VAL_MAG_MODE_ST 0x10
#define REG_ADD_MAG_CNTL3 0x32
#define REG_VAL_MAG_CNTL3_RESET 0x01
#define TEMP_SENSITIVITY_REG 333.87f

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_Macros
 * \{
 */
#define F_IIC_WriteByte(a, b, c)                  \
    do                                            \
    {                                             \
        w_data = c;                               \
        _bICM20948WriteRegs(pdrv, b, &w_data, 1); \
    } while (0);

#define F_IIC_ReadByte(a, b, e)                      \
    do                                               \
    {                                                \
        e = _bICM20948ReadRegs(pdrv, b, &r_data, 1); \
    } while (0);

#define ICM20948_Mag_Read(a, b, c, d)         \
    do                                        \
    {                                         \
        bICM20948_Mag_Read(pdrv, a, b, c, d); \
    } while (0);

#define ICM20948_Mag_Write(a, b, c)         \
    do                                      \
    {                                       \
        bICM20948_Mag_Write(pdrv, a, b, c); \
    } while (0);

#define ICM20948_Mag_Reset()       \
    do                             \
    {                              \
        bICM20948_Mag_Reset(pdrv); \
    } while (0);

#define F_IIC_ReadBytes(a, b, c, d, e)         \
    do                                         \
    {                                          \
        e = _bICM20948ReadRegs(pdrv, b, c, d); \
    } while (0);

#define F_Delay_ms(a) bHalDelayMs(a)

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bICM20948_HalIf_t, DRIVER_NAME);

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ICM20948_Private_Functions
 * \{
 */

//----------------------------------------------------------------
static int _bICM20948ReadRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data, uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bICM20948_HalIf_t, pdrv);
    // reg = reg | 0x80;
    if (bHalI2CMemRead(_if, reg, 1, data, len) < 0)
    {
        return -1;
    }
    else
    {
        return len;
    }
}

static int _bICM20948WriteRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data, uint16_t len)
{
    bDRIVER_GET_HALIF(_if, bICM20948_HalIf_t, pdrv);

    bHalI2CMemWrite(_if, reg, 1, data, len);

    return len;
}

static int _ICM20948GetID(bDriverInterface_t *pdrv, uint8_t *id)
{
    int     retval = 0;
    // uint8_t w_data = 0;
    uint8_t r_data = 0;

    // F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);
    F_IIC_ReadByte(I2C_ADD_ICM20948, REG_ADD_WHO_AM_I, retval);

    if (retval <= 0)
    {
        return -1;
    }

    *id = r_data;

    return retval;
}

int F_ICM20948_GetData(bDriverInterface_t *pdrv, int32_t *pData_acc, int32_t *pData_gyro,
                       int32_t *pData_mag, float *tempreature)
{
    int     retval = 0;
    uint8_t tempBuf[20];
    int16_t pDataRaw_x[3];
    int16_t pDataRaw_g[3];
    int16_t pDataRaw_m[3];
    int16_t tempreature_r;

    F_IIC_ReadBytes(I2C_ADD_ICM20948, REG_ADD_ACCEL_XOUT_H, &tempBuf[0], 20, retval);
    if (retval < 0)
    {
        return -1;
    }

    pDataRaw_x[0] = ((((int16_t)tempBuf[0]) << 8) + (int16_t)tempBuf[1]);
    pDataRaw_x[1] = ((((int16_t)tempBuf[2]) << 8) + (int16_t)tempBuf[3]);
    pDataRaw_x[2] = ((((int16_t)tempBuf[4]) << 8) + (int16_t)tempBuf[5]);

    pDataRaw_g[0] = ((((int16_t)tempBuf[6]) << 8) + (int16_t)tempBuf[7]);
    pDataRaw_g[1] = ((((int16_t)tempBuf[8]) << 8) + (int16_t)tempBuf[9]);
    pDataRaw_g[2] = ((((int16_t)tempBuf[10]) << 8) + (int16_t)tempBuf[11]);

    tempreature_r = ((((int16_t)tempBuf[12]) << 8) + (int16_t)tempBuf[13]);

    pDataRaw_m[0] = ((((int16_t)tempBuf[15]) << 8) + (int16_t)tempBuf[14]);
    pDataRaw_m[1] = ((((int16_t)tempBuf[17]) << 8) + (int16_t)tempBuf[16]);
    pDataRaw_m[2] = ((((int16_t)tempBuf[19]) << 8) + (int16_t)tempBuf[18]);

    *tempreature = (float)(tempreature_r / TEMP_SENSITIVITY_REG);

    pData_gyro[0] = pDataRaw_g[0] * 60.9756097561f;
    pData_gyro[1] = pDataRaw_g[1] * 60.9756097561f;
    pData_gyro[2] = pDataRaw_g[2] * 60.9756097561f;

    pData_acc[0] = (int32_t)(pDataRaw_x[0] * 0.061035156f);
    pData_acc[1] = (int32_t)(pDataRaw_x[1] * 0.061035156f);
    pData_acc[2] = (int32_t)(pDataRaw_x[2] * 0.061035156f);

    pData_mag[0] = (int32_t)(pDataRaw_m[0] * 1.0f);
    pData_mag[1] = (int32_t)(pDataRaw_m[1] * 1.0f);
    pData_mag[2] = (int32_t)(pDataRaw_m[2] * 1.0f);

    return retval;
}

static void bICM20948_Mag_Read(bDriverInterface_t *pdrv, uint8_t I2CAddr, uint8_t RegAddr,
                               uint8_t Len, uint8_t *pdata)
{
    uint8_t w_data = 0;

    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3);  // swtich bank3
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_ADDR,
                    I2CAddr | I2C_ADD_ICM20948_AK09916_READ);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_REG, RegAddr);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_CTRL, REG_VAL_BIT_SLV0_EN | Len);
    F_Delay_ms(10);

    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);  // swtich bank0

    for (uint8_t i = 0; i < Len; i++)
    {
        _bICM20948ReadRegs(pdrv, REG_ADD_EXT_SENS_DATA_00 + i, pdata + i, 1);
    }
}

static void bICM20948_Mag_Write(bDriverInterface_t *pdrv, uint8_t I2CAddr, uint8_t RegAddr,
                                uint8_t data)
{
    uint8_t w_data = 0;
    uint8_t r_data = 0;

    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3);  // swtich bank3
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_ADDR, I2CAddr);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_REG, RegAddr);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_DO, data);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_CTRL, REG_VAL_BIT_SLV0_EN | 1);
    F_Delay_ms(50);
    ICM20948_Mag_Read(I2CAddr, RegAddr, 1, &r_data);
}

static void bICM20948_Mag_Reset(bDriverInterface_t *pdrv)
{
    uint8_t w_data = 0;

    ICM20948_Mag_Write(I2C_ADD_ICM20948_AK09916, REG_ADD_MAG_CNTL2, 0x00);

    F_Delay_ms(10);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_PWR_MGMT_1, REG_VAL_ALL_RGE_RESET);
    F_Delay_ms(10);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_PWR_MGMT_1, REG_VAL_RUN_MODE);
    F_Delay_ms(10);
    F_IIC_WriteByte(I2C_ADD_ICM20948, REG_ADD_PWR_MGMT_2, REG_VAL_MGMNT_2_SEN_ENABLE);
    F_Delay_ms(10);
    ICM20948_Mag_Write(I2C_ADD_ICM20948_AK09916, REG_ADD_MAG_CNTL3, REG_VAL_MAG_CNTL3_RESET);
    F_Delay_ms(10);
}

static int _bICM20948Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    int                retval = 0;
    bICM20948_9Axis_t *ptmp   = (bICM20948_9Axis_t *)pbuf;

    if (len < sizeof(bICM20948_9Axis_t))
    {
        return 0;
    }
    retval =
        F_ICM20948_GetData(pdrv, ptmp->acc_arr, ptmp->gyro_arr, ptmp->mag_arr, &ptmp->temperature);
    return retval;
}

static int _bICM20948Ctl(struct bDriverIf *pdrv, uint8_t cmd, void *param)
{
    int retval = 0;
    switch (cmd)
    {
        case bCMD_ICM20948_SET_STATUS_ERR:
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
 * \}
 */

/**
 * \addtogroup ICM20948_Exported_Functions
 * \{
 */
int bICM20948_Init(bDriverInterface_t *pdrv)
{
    uint8_t w_data = 0;
    uint8_t r_data = 0;
    uint8_t data[8];

    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bICM20948_Init);
    pdrv->read = _bICM20948Read;
    pdrv->ctl  = _bICM20948Ctl;

    if (_ICM20948GetID(pdrv, &r_data) < 0)
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
    _bICM20948ReadRegs(pdrv, 0x03, &r_data, 1);
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

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_ICM20948, bICM20948_Init);
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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
