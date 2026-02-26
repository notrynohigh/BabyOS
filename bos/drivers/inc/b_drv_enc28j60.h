/**
 *!
 * \file        b_drv_enc28j60.h
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
#ifndef __B_DRV_ENC28J60_H__
#define __B_DRV_ENC28J60_H__

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
 * \addtogroup ENC28J60
 * \{
 */

/**
 * \defgroup ENC28J60_Exported_TypesDefinitions
 * \{
 */

typedef struct
{
    bHalSPIIf_t        _spi;
    bHalGPIOInstance_t reset;
} bENC28J60_HalIf_t;

typedef struct
{
    uint8_t        cur_bank;
    uint8_t        link_state;
    uint8_t        mac_addr[6];
    uint16_t       next_packet_ptr;
    bLinkStateCb_t link_cb;
    bHalBufList_t  list_opt;
} bENC28J60Private_t;

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
