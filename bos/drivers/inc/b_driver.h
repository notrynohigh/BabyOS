/**
 *!
 * \file        b_driver.h
 * \version     v0.0.1
 * \date        2019/06/05
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
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
#ifndef __B_DRIVER_H__
#define __B_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "b_driver_cmd.h"
#include "b_type.h"
#include "hal/inc/b_hal.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup CORE
 * \{
 */

/**
 * \addtogroup DRIVER
 * \{
 */

/**
 * \defgroup DRIVER_Exported_TypesDefinitions
 * \{
 */

typedef struct
{
    struct bDriverIf *pdrv;
    uint16_t          offset;
    uint8_t          *pbuf;
    uint16_t          len;
} bOptParam_t;

typedef struct
{
    struct bDriverIf *pdrv;
    uint8_t           cmd;
    void             *param;
} bCtlParam_t;

typedef struct bDriverIf
{
    uint8_t  flag : 2;
    uint8_t  status : 2;  // 0: normal 1:open  2: error
    uint8_t  drv_no : 4;
    uint16_t offset;
    int (*init)(struct bDriverIf *pdrv);
    int (*ctl)(bCtlParam_t *param);
    int (*write)(bOptParam_t *param);
    int (*read)(bOptParam_t *param);
    void *hal_if;
    union
    {
        uint16_t v;
        void    *_p;
    } _private;
} bDriverInterface_t;

typedef int (*bDriverInit_t)(struct bDriverIf *pdrv);

typedef enum
{
    B_DRIVER_NUMBER
} bDriverNumber_t;

/**
 * \}
 */

/**
 * \defgroup DRIVER_Exported_Defines
 * \{
 */

#define _bDRIVER_STRUCT_INIT(pdrv) \
    do                             \
    {                              \
        pdrv->status     = 0;      \
        pdrv->ctl        = NULL;   \
        pdrv->write      = NULL;   \
        pdrv->read       = NULL;   \
        pdrv->_private.v = 0;      \
    } while (0)
#define bDRIVER_STRUCT_INIT(pdrv) _bDRIVER_STRUCT_INIT(pdrv)

#define bDRIVER_GET_HALIF(name, type, pdrv) type *name = (type *)((pdrv)->hal_if)
#define bDRIVER_GET_PRIVATE(name, type, pdrv) type *name = (type *)((pdrv)->_private._p)

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

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
