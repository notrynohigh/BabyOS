/**
 *!
 * \file        b_mod_heartbeat.h
 * \version     v0.0.1
 * \date        2025/03/15
 * \author      aiclaw(aiclaw@babyos.dev)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 aiclaw
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
#ifndef __B_MOD_HEARTBEAT_H__
#define __B_MOD_HEARTBEAT_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "b_config.h"

#if (defined(_HEARTBEAT_ENABLE) && (_HEARTBEAT_ENABLE == 1))

/*Exported types ----------------------------------------*/
typedef enum
{
    B_HB_STA_OFFLINE,
    B_HB_STA_ONLINE,
    B_HB_STA_RECOVERING,
} bHeartbeatStatus_t;

typedef void (*bHeartbeatCallback_t)(uint32_t dev_no, bHeartbeatStatus_t status, void *user_data);

typedef struct
{
    uint32_t              dev_no;
    uint32_t              interval_ms;
    uint32_t              timeout_ms;
    bHeartbeatCallback_t  callback;
    void                 *user_data;
} bHeartbeatConfig_t;

/*Exported functions ------------------------------------*/
int  bHeartbeatInit(void);
int  bHeartbeatRegister(const bHeartbeatConfig_t *config);
int  bHeartbeatUnregister(uint32_t dev_no);
int  bHeartbeatUpdate(uint32_t dev_no);
void bHeartbeatPrintStatus(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
/************************ Copyright (c) 2025 aiclaw *****END OF FILE****/
