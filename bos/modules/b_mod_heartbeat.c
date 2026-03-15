/**
 *!
 * \file        b_mod_heartbeat.c
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

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_heartbeat.h"

#if (defined(_HEARTBEAT_ENABLE) && (_HEARTBEAT_ENABLE == 1))

#include <string.h>

#include "core/inc/b_device.h"
#include "core/inc/b_timer.h"
#include "utils/inc/b_util_log.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup HEARTBEAT
 * \{
 */

/**
 * \defgroup HEARTBEAT_Private_TypesDefinitions
 * \{
 */

#ifndef B_HEARTBEAT_MAX_DEVICES
#define B_HEARTBEAT_MAX_DEVICES (8)
#endif

typedef struct
{
    uint8_t              used;
    uint32_t             dev_no;
    uint32_t             timeout_ms;
    uint32_t             last_update;
    bHeartbeatStatus_t   status;
    bHeartbeatCallback_t callback;
    void                *user_data;
} bHeartbeatDevice_t;

/**
 * \}
 */

/**
 * \defgroup HEARTBEAT_Private_Variables
 * \{
 */

static bHeartbeatDevice_t gHbDevices[B_HEARTBEAT_MAX_DEVICES];
static bTimerId_t         gHbTimer = NULL;
static volatile uint32_t  gHbTick  = 0;

/**
 * \}
 */

/**
 * \defgroup HEARTBEAT_Private_Functions
 * \{
 */

static bHeartbeatDevice_t *_bHbFindDevice(uint32_t dev_no)
{
    int i;
    for (i = 0; i < B_HEARTBEAT_MAX_DEVICES; i++)
    {
        if (gHbDevices[i].used && gHbDevices[i].dev_no == dev_no)
        {
            return &gHbDevices[i];
        }
    }
    return NULL;
}

static void _bHbTimerCallback(void *arg)
{
    (void)arg;
    gHbTick++;
    
    int                 i;
    bHeartbeatDevice_t *pdev;
    bHeartbeatStatus_t  new_status;
    
    for (i = 0; i < B_HEARTBEAT_MAX_DEVICES; i++)
    {
        if (!gHbDevices[i].used)
        {
            continue;
        }
        
        pdev = &gHbDevices[i];
        
        /* Check if device has timed out */
        if ((gHbTick - pdev->last_update) * 100 >= pdev->timeout_ms)
        {
            new_status = B_HB_STA_OFFLINE;
        }
        else
        {
            new_status = B_HB_STA_ONLINE;
        }
        
        /* Status change callback */
        if (pdev->status != new_status)
        {
            bHeartbeatStatus_t old_status = pdev->status;
            pdev->status = new_status;
            
            if (pdev->callback)
            {
                pdev->callback(pdev->dev_no, new_status, pdev->user_data);
            }
            
            b_log_i("Device %d status: %s -> %s\r\n", pdev->dev_no,
                    old_status == B_HB_STA_ONLINE ? "ONLINE" : "OFFLINE",
                    new_status == B_HB_STA_ONLINE ? "ONLINE" : "OFFLINE");
        }
    }
}

/**
 * \}
 */

/**
 * \addtogroup HEARTBEAT_Exported_Functions
 * \{
 */

int bHeartbeatInit(void)
{
    memset(gHbDevices, 0, sizeof(gHbDevices));
    gHbTick = 0;
    
    /* Create periodic timer (100ms interval) */
    gHbTimer = bTimerCreate(NULL, B_TIMER_PERIODIC, _bHbTimerCallback, NULL);
    if (gHbTimer == NULL)
    {
        b_log_e("Heartbeat timer create failed\r\n");
        return -1;
    }
    
    bTimerStart(gHbTimer, 100);
    b_log_i("Heartbeat module initialized\r\n");
    return 0;
}

int bHeartbeatRegister(const bHeartbeatConfig_t *config)
{
    if (config == NULL)
    {
        return -1;
    }
    
    /* Check if already registered */
    if (_bHbFindDevice(config->dev_no) != NULL)
    {
        b_log_e("Device %d already registered\r\n", config->dev_no);
        return -2;
    }
    
    /* Find free slot */
    int i;
    for (i = 0; i < B_HEARTBEAT_MAX_DEVICES; i++)
    {
        if (!gHbDevices[i].used)
        {
            gHbDevices[i].used        = 1;
            gHbDevices[i].dev_no      = config->dev_no;
            gHbDevices[i].timeout_ms  = config->timeout_ms > 0 ? config->timeout_ms : 5000;
            gHbDevices[i].last_update = gHbTick;
            gHbDevices[i].status      = B_HB_STA_ONLINE;
            gHbDevices[i].callback    = config->callback;
            gHbDevices[i].user_data   = config->user_data;
            
            b_log_i("Device %d registered (timeout: %dms)\r\n", 
                    config->dev_no, gHbDevices[i].timeout_ms);
            return 0;
        }
    }
    
    b_log_e("No free slot for device %d\r\n", config->dev_no);
    return -3;
}

int bHeartbeatUnregister(uint32_t dev_no)
{
    bHeartbeatDevice_t *pdev = _bHbFindDevice(dev_no);
    if (pdev == NULL)
    {
        return -1;
    }
    
    memset(pdev, 0, sizeof(bHeartbeatDevice_t));
    b_log_i("Device %d unregistered\r\n", dev_no);
    return 0;
}

int bHeartbeatUpdate(uint32_t dev_no)
{
    bHeartbeatDevice_t *pdev = _bHbFindDevice(dev_no);
    if (pdev == NULL)
    {
        return -1;
    }
    
    pdev->last_update = gHbTick;
    return 0;
}

void bHeartbeatPrintStatus(void)
{
    int i;
    
    b_log("\r\n======== Device Heartbeat Status ========\r\n");
    b_log("%-8s %-10s %-12s\r\n", "DevNo", "Status", "LastUpdate");
    b_log("----------------------------------------\r\n");
    
    for (i = 0; i < B_HEARTBEAT_MAX_DEVICES; i++)
    {
        if (gHbDevices[i].used)
        {
            b_log("%-8d %-10s %ums ago\r\n", 
                  gHbDevices[i].dev_no,
                  gHbDevices[i].status == B_HB_STA_ONLINE ? "ONLINE" : "OFFLINE",
                  (gHbTick - gHbDevices[i].last_update) * 100);
        }
    }
    
    b_log("=========================================\r\n");
}

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#endif

/************************ Copyright (c) 2025 aiclaw *****END OF FILE****/
