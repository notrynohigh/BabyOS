/**
 *!
 * \file        b_drv_paj7620u2.c
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
#include "drivers/inc/b_drv_paj7620u2.h"

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup PAJ7620U2
 * \{
 */

/**
 * \defgroup PAJ7620U2_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PAJ7620U2_Private_Defines
 * \{
 */
#define DRIVER_NAME PAJ7620U2

#define PAJ_REGITER_BANK_SEL 0XEF  // Bank Select
#define PAJ_BANK0 0X00             // BANK0
#define PAJ_BANK1 0X01             // BANK1

#define PAJ_CHIPID_L 0X00
#define PAJ_CHIPID_H 0X01
#define PAJ_CHIPID_VALUE (0X7620)

// BANK0
#define PAJ_SUSPEND_CMD 0X03
#define PAJ_SET_INT_FLAG1 0X41
#define PAJ_SET_INT_FLAG2 0X42
#define PAJ_GET_INT_FLAG1 0X43
#define PAJ_GET_INT_FLAG2 0X44
#define PAJ_GET_STATE 0X45
#define PAJ_SET_HIGH_THRESHOLD 0x69
#define PAJ_SET_LOW_THRESEHOLD 0X6A
#define PAJ_GET_APPROACH_STATE 0X6B
#define PAJ_GET_GESTURE_DATA 0X6C
#define PAJ_GET_OBJECT_BRIGHTNESS 0XB0
#define PAJ_GET_OBJECT_SIZE_1 0XB1
#define PAJ_GET_OBJECT_SIZE_2 0XB2

// BANK1
#define PAJ_SET_PS_GAIN 0X44
#define PAJ_SET_IDLE_S1_STEP_0 0x67
#define PAJ_SET_IDLE_S1_STEP_1 0x68
#define PAJ_SET_IDLE_S2_STEP_0 0X69
#define PAJ_SET_IDLE_S2_STEP_1 0X6A
#define PAJ_SET_OP_TO_S1_STEP_0 0X6B
#define PAJ_SET_OP_TO_S1_STEP_1 0X6C
#define PAJ_SET_S1_TO_S2_STEP_0 0X6D
#define PAJ_SET_S1_TO_S2_STEP_1 0X6E
#define PAJ_OPERATION_ENABLE 0X72

#define GES_UP (0x0001)
#define GES_DOWM (0x0002)
#define GES_LEFT (0x0004)
#define GES_RIGHT (0x0008)
#define GES_FORWARD (0x0010)
#define GES_BACKWARD (0x0020)
#define GES_CLOCKWISE (0x0040)
#define GES_COUNT_CLOCKWISE (0x0080)
#define GES_WAVE (0x0100)

/**
 * \}
 */

/**
 * \defgroup PAJ7620U2_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PAJ7620U2_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bPAJ7620U2_HalIf_t, DRIVER_NAME);

static bPAJ7620U2Private_t bPAJ7620U2RunInfo[bDRIVER_HALIF_NUM(bPAJ7620U2_HalIf_t, DRIVER_NAME)];

const uint8_t bPAJInitTable[][2] = {

    {0xEF, 0x00}, {0x37, 0x07}, {0x38, 0x17}, {0x39, 0x06}, {0x41, 0x00}, {0x42, 0x00},
    {0x46, 0x2D}, {0x47, 0x0F}, {0x48, 0x3C}, {0x49, 0x00}, {0x4A, 0x1E}, {0x4C, 0x20},
    {0x51, 0x10}, {0x5E, 0x10}, {0x60, 0x27}, {0x80, 0x42}, {0x81, 0x44}, {0x82, 0x04},
    {0x8B, 0x01}, {0x90, 0x06}, {0x95, 0x0A}, {0x96, 0x0C}, {0x97, 0x05}, {0x9A, 0x14},
    {0x9C, 0x3F}, {0xA5, 0x19}, {0xCC, 0x19}, {0xCD, 0x0B}, {0xCE, 0x13}, {0xCF, 0x64},
    {0xD0, 0x21}, {0xEF, 0x01}, {0x02, 0x0F}, {0x03, 0x10}, {0x04, 0x02}, {0x25, 0x01},
    {0x27, 0x39}, {0x28, 0x7F}, {0x29, 0x08}, {0x3E, 0xFF}, {0x5E, 0x3D}, {0x65, 0x96},
    {0x67, 0x97}, {0x69, 0xCD}, {0x6A, 0x01}, {0x6D, 0x2C}, {0x6E, 0x01}, {0x72, 0x01},
    {0x73, 0x35}, {0x74, 0x00}, {0x77, 0x01},
};

const uint8_t bGestureTable[][2] = {

    {0xEF, 0x00}, {0x41, 0x00}, {0x42, 0x00}, {0xEF, 0x00}, {0x48, 0x3C}, {0x49, 0x00},
    {0x51, 0x10}, {0x83, 0x20}, {0x9F, 0xF9}, {0xEF, 0x01}, {0x01, 0x1E}, {0x02, 0x0F},
    {0x03, 0x10}, {0x04, 0x02}, {0x41, 0x40}, {0x43, 0x30}, {0x65, 0x96}, {0x66, 0x00},
    {0x67, 0x97}, {0x68, 0x01}, {0x69, 0xCD}, {0x6A, 0x01}, {0x6B, 0xB0}, {0x6C, 0x04},
    {0x6D, 0x2C}, {0x6E, 0x01}, {0x74, 0x00}, {0xEF, 0x00}, {0x41, 0xFF}, {0x42, 0x01},
};

/**
 * \}
 */

/**
 * \defgroup PAJ7620U2_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PAJ7620U2_Private_Functions
 * \{
 */

static void _bPAJ7620U2SelectBank(bDriverInterface_t *pdrv, uint8_t bank)
{
    bDRIVER_GET_HALIF(_if, bPAJ7620U2_HalIf_t, pdrv);
    if (bank > 1)
    {
        return;
    }
    bHalI2CMemWrite(_if, PAJ_REGITER_BANK_SEL, 1, &bank, 1);
}

static int _bPAJ7620U2ReadId(bDriverInterface_t *pdrv)
{
    int      i       = 0;
    uint16_t chip_id = 0;
    bDRIVER_GET_HALIF(_if, bPAJ7620U2_HalIf_t, pdrv);
    for (i = 0; i < 3; i++)
    {
        _bPAJ7620U2SelectBank(pdrv, 0);
        bHalI2CMemRead(_if, PAJ_CHIPID_L, 1, (uint8_t *)&chip_id, 2);
        b_log("chip id:%x\r\n", chip_id);
        if (chip_id == PAJ_CHIPID_VALUE)
        {
            return 0;
        }
    }
    return -1;
}

static void _bPAJ7620U2SetGestureMode(bDriverInterface_t *pdrv)
{
    int i = 0;
    bDRIVER_GET_HALIF(_if, bPAJ7620U2_HalIf_t, pdrv);
    for (i = 0; i < (sizeof(bGestureTable) / (sizeof(uint8_t) * 2)); i++)
    {
        bHalI2CMemWrite(_if, bGestureTable[i][0], 1, &bGestureTable[i][1], 1);
    }
    _bPAJ7620U2SelectBank(pdrv, 0);
}

static int _bPAJ7620U2Init(bDriverInterface_t *pdrv)
{
    int i = 0;
    bDRIVER_GET_HALIF(_if, bPAJ7620U2_HalIf_t, pdrv);
    if (_bPAJ7620U2ReadId(pdrv) < 0)
    {
        return -1;
    }
    for (i = 0; i < (sizeof(bPAJInitTable) / (sizeof(uint8_t) * 2)); i++)
    {
        bHalI2CMemWrite(_if, bPAJInitTable[i][0], 1, &bPAJInitTable[i][1], 1);
    }
    _bPAJ7620U2SelectBank(pdrv, 0);
    return 0;
}

static int _bPAJ7620U2Write(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bPAJ7620U2_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, bPAJ7620U2Private_t, pdrv);
    return len;
}

static int _bPAJ7620U2Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    uint16_t gesture = 0;
    bDRIVER_GET_HALIF(_if, bPAJ7620U2_HalIf_t, pdrv);
    bHalI2CMemRead(_if, PAJ_GET_INT_FLAG1, 1, (uint8_t *)&gesture, 2);
    if (pbuf == NULL || len < 2)
    {
        return -1;
    }
    if (gesture)
    {
        *((uint16_t *)pbuf) = gesture;
        return 2;
    }
    return 0;
}

static int _bPAJ7620U2Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bDRIVER_GET_PRIVATE(_priv, bPAJ7620U2Private_t, pdrv);
    return 0;
}

static int _bPAJ7620U2Open(bDriverInterface_t *pdrv)
{
    _bPAJ7620U2SetGestureMode(pdrv);
    return 0;
}

static int _bPAJ7620U2Close(bDriverInterface_t *pdrv)
{
    uint8_t tmp = 0;
    bDRIVER_GET_HALIF(_if, bPAJ7620U2_HalIf_t, pdrv);
    bHalI2CMemWrite(_if, PAJ_SET_INT_FLAG1, 1, &tmp, 1);
    bHalI2CMemWrite(_if, PAJ_SET_INT_FLAG2, 1, &tmp, 1);
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup PAJ7620U2_Exported_Functions
 * \{
 */
int bPAJ7620U2_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bPAJ7620U2_Init);
    pdrv->read        = _bPAJ7620U2Read;
    pdrv->write       = _bPAJ7620U2Write;
    pdrv->ctl         = _bPAJ7620U2Ctl;
    pdrv->open        = _bPAJ7620U2Open;
    pdrv->close       = _bPAJ7620U2Close;
    pdrv->_private._p = &bPAJ7620U2RunInfo[pdrv->drv_no];
    return _bPAJ7620U2Init(pdrv);
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_PAJ7620U2, bPAJ7620U2_Init);
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
