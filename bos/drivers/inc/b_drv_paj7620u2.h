/**
 *!
 * \file        b_drv_paj7620u2.h
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
#ifndef __B_DRV_PAJ7620U2_H__
#define __B_DRV_PAJ7620U2_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_driver.h"
/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup PAJ7620U2
 * \{
 */

/**
 * \defgroup PAJ7620U2_Exported_TypesDefinitions
 * \{
 */

typedef bHalI2CIf_t bPAJ7620U2_HalIf_t;

typedef struct
{
    uint8_t reserved;
} bPAJ7620U2Private_t;

#define PAJ7620U2_READ_GESTURE_UP (0x0001)
#define PAJ7620U2_READ_GESTURE_DOWN (0x0002)
#define PAJ7620U2_READ_GESTURE_LEFT (0x0004)
#define PAJ7620U2_READ_GESTURE_RIGHT (0x0008)
#define PAJ7620U2_READ_GESTURE_FORWARD (0x0010)
#define PAJ7620U2_READ_GESTURE_BACKWARD (0x0020)
#define PAJ7620U2_READ_GESTURE_CLOCKWISE (0x0040)
#define PAJ7620U2_READ_GESTURE_COUNT_CLOCKWISE (0x0080)
#define PAJ7620U2_READ_GESTURE_WAVE (0x0100)

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2023 babyos *****END OF FILE****/
