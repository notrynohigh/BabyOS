/**
 *!
 * \file        b_mod_wifi.c
 * \version     v0.0.1
 * \date        2020/05/16
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
#include "modules/inc/b_mod_wifi.h"

#if (defined(_WIFI_ENABLE) && (_WIFI_ENABLE == 1))
#include "core/inc/b_core.h"
#include "core/inc/b_task.h"
#include "drivers/inc/b_driver_cmd.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup WIFI
 * \{
 */

/**
 * \defgroup WIFI_Private_TypesDefinitions
 * \{
 */

typedef struct
{
    uint8_t  linked;
    uint32_t dev_no;

    bWifiMode_t mode;
    bWifiMode_t cur_mode;

    bApInfo_t soft_ap;
    bApInfo_t ap;

    pWifiEvtCb_t cb;
    void        *user_data;

    bTaskAttr_t attr;
    bTaskId_t   task_id;
} bWifiModule_t;

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Variables
 * \{
 */

static bWifiModule_t bWifiModule = {
    .dev_no = 0,
};

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Functions
 * \{
 */

static void _bWifiDrvCb(bWifiDrvEvent_t event, void *arg, void (*release)(void *), void *user_data)
{
    uint8_t result = 0;
    b_log_w("evt:%d %p\r\n", event, arg);
    switch (event)
    {
        case B_EVT_MODE_STA_OK:
        {
            if (bWifiModule.mode == B_WIFI_MODE_STA)
            {
                result               = 1;
                bWifiModule.cur_mode = bWifiModule.mode;
                B_SAFE_INVOKE(bWifiModule.cb, B_WIFI_EVT_SWITCH_MODE, &result, NULL,
                              bWifiModule.user_data);
            }
        }
        break;
        case B_EVT_MODE_AP_OK:
        {
            if (bWifiModule.mode == B_WIFI_MODE_AP)
            {
                result               = 1;
                bWifiModule.cur_mode = bWifiModule.mode;
                B_SAFE_INVOKE(bWifiModule.cb, B_WIFI_EVT_SWITCH_MODE, &result, NULL,
                              bWifiModule.user_data);
            }
        }
        break;
        case B_EVT_MODE_STA_AP_OK:
        {
            if (bWifiModule.mode == B_WIFI_MODE_STA_AP)
            {
                result               = 1;
                bWifiModule.cur_mode = bWifiModule.mode;
                B_SAFE_INVOKE(bWifiModule.cb, B_WIFI_EVT_SWITCH_MODE, &result, NULL,
                              bWifiModule.user_data);
            }
        }
        break;
        case B_EVT_JOIN_AP_OK:
        {
            bWifiModule.linked = 1;
            B_SAFE_INVOKE(bWifiModule.cb, B_WIFI_EVT_CONN_AP, &bWifiModule.linked, NULL,
                          bWifiModule.user_data);
        }
        break;
        case B_EVT_FAIL_BASE:
        {
            ;
        }
        break;
        case B_EVT_MODE_STA_FAIL:
        case B_EVT_MODE_AP_FAIL:
        case B_EVT_MODE_STA_AP_FAIL:
        {
            result = 0;
            B_SAFE_INVOKE(bWifiModule.cb, B_WIFI_EVT_SWITCH_MODE, &result, NULL,
                          bWifiModule.user_data);
        }
        break;
        case B_EVT_JOIN_AP_FAIL:
        {
            bWifiModule.linked = 0;
            B_SAFE_INVOKE(bWifiModule.cb, B_WIFI_EVT_CONN_AP, &bWifiModule.linked, NULL,
                          bWifiModule.user_data);
        }
        break;
        default:
            break;
    }
}

static PT_THREAD(_bWifiTask)(struct pt *pt, void *arg)
{
    uint8_t link_state = 0;
    int     fd = -1, ret = -1;
    B_TASK_INIT_BEGIN();
    // ...
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        if (bWifiModule.cur_mode != bWifiModule.mode)
        {
            fd = bOpen(bWifiModule.dev_no, BCORE_FLAG_RW);
            if (fd < 0)
            {
                b_log_e("open wifi dev fail\r\n");
                bTaskRestart(pt);
            }
            if (bWifiModule.mode == B_WIFI_MODE_STA)
            {
                ret = bCtl(fd, bCMD_WIFI_MODE_STA, NULL);
                bClose(fd);
                if (0 == ret)
                {
                    PT_WAIT_UNTIL(pt, bWifiModule.cur_mode == bWifiModule.mode, MS2TICKS(5000));
                }
            }
            else if (bWifiModule.mode == B_WIFI_MODE_AP)
            {
                ret = bCtl(fd, bCMD_WIFI_MODE_AP, &bWifiModule.soft_ap);
                bClose(fd);
                if (0 == ret)
                {
                    PT_WAIT_UNTIL(pt, bWifiModule.cur_mode == bWifiModule.mode, MS2TICKS(5000));
                }
            }
            else if (bWifiModule.mode == B_WIFI_MODE_STA_AP)
            {
                ret = bCtl(fd, bCMD_WIFI_MODE_STA_AP, &bWifiModule.soft_ap);
                bClose(fd);
                if (0 == ret)
                {
                    PT_WAIT_UNTIL(pt, bWifiModule.cur_mode == bWifiModule.mode, MS2TICKS(5000));
                }
            }
            else
            {
                b_log_e("wifi mode error\r\n");
                bClose(fd);
            }
        }
        else
        {
            if ((bWifiModule.cur_mode == B_WIFI_MODE_STA ||
                 bWifiModule.cur_mode == B_WIFI_MODE_STA_AP) &&
                (strlen(&bWifiModule.ap.ssid[0]) > 0))
            {
                if (bWifiModule.linked == 0)
                {
                    fd = bOpen(bWifiModule.dev_no, BCORE_FLAG_RW);
                    if (fd < 0)
                    {
                        b_log_e("open wifi dev fail\r\n");
                        bTaskRestart(pt);
                    }
                    ret = bCtl(fd, bCMD_WIFI_JOIN_AP, &bWifiModule.ap);
                    bClose(fd);
                    if (0 == ret)
                    {
                        PT_WAIT_UNTIL(pt, bWifiModule.linked == 1, MS2TICKS(10000));
                    }
                }
            }
        }
        bTaskYield(pt);
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup WIFI_Exported_Functions
 * \{
 */

int bWifiInit(uint32_t dev_no, pWifiEvtCb_t cb, void *user_data)
{
    bWifiDrvCallback_t drv_cb;
    if (cb == NULL)
    {
        return -1;
    }
    int fd = bOpen(dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -1;
    }
    bWifiModule.dev_no    = dev_no;
    bWifiModule.cb        = cb;
    bWifiModule.user_data = user_data;
    drv_cb.cb             = _bWifiDrvCb;
    drv_cb.user_data      = NULL;
    if (bCtl(fd, bCMD_WIFI_REG_CALLBACK, &drv_cb) != 0)
    {
        bClose(fd);
        return -1;
    }
    bClose(fd);
    return 0;
}

int bWifiSetMode(bWifiMode_t mode)
{
    bWifiModule.cur_mode = B_WIFI_MODE_UNKNOWN;
    bWifiModule.mode     = mode;
    if (bWifiModule.task_id == NULL)
    {
        bWifiModule.task_id = bTaskCreate("wifi", _bWifiTask, NULL, &bWifiModule.attr);
        // REVIEW-V3 #7 fix: 之前永远返 0, 让 config-web 的 H-NEW-5 wifi_ret 闸门形同虚设.
        // task 创建失败 (OOM / scheduler 满) 时返回 -1, 上层可感知并报告给前端.
        if (bWifiModule.task_id == NULL)
        {
            return -1;
        }
    }
    return 0;
}

int bWifiDeinit()
{
    return 0;
}

int bWifiApConfig(const char *ssid, const char *passwd)
{
    if (ssid == NULL)
    {
        return -1;
    }
    if (strlen(ssid) > WIFI_SSID_LEN_MAX)
    {
        return -1;
    }
    if (passwd != NULL)
    {
        if (strlen(passwd) > WIFI_PASSWD_LEN_MAX)
        {
            return -1;
        }
        if (strlen(passwd) == 0)
        {
            passwd = NULL;
        }
    }
    memset(&bWifiModule.soft_ap, 0, sizeof(bWifiModule.soft_ap));
    memcpy(&bWifiModule.soft_ap.ssid[0], ssid, strlen(ssid));
    if (passwd == NULL)
    {
        bWifiModule.soft_ap.encryption = 0;
    }
    else
    {
        bWifiModule.soft_ap.encryption = 3;
        memcpy(&bWifiModule.soft_ap.passwd[0], passwd, strlen(passwd));
    }
    bWifiModule.cur_mode = B_WIFI_MODE_UNKNOWN;
    return 0;
}

int bWifiJoinAp(const char *ssid, const char *passwd)
{
    int retval = -1;
    if (ssid == NULL)
    {
        return -1;
    }
    if (strlen(ssid) > WIFI_SSID_LEN_MAX)
    {
        return -1;
    }
    if (passwd != NULL)
    {
        if (strlen(passwd) > WIFI_PASSWD_LEN_MAX)
        {
            return -1;
        }
        if (strlen(passwd) == 0)
        {
            passwd = NULL;
        }
    }
    memset(&bWifiModule.ap, 0, sizeof(bWifiModule.ap));
    memcpy(&bWifiModule.ap.ssid[0], ssid, strlen(ssid));
    if (passwd == NULL)
    {
        bWifiModule.ap.encryption = 0;
    }
    else
    {
        bWifiModule.ap.encryption = 3;
        memcpy(&bWifiModule.ap.passwd[0], passwd, strlen(passwd));
    }
    bWifiModule.cur_mode = B_WIFI_MODE_UNKNOWN;
    return 0;
}

uint8_t bWifiIsConnected(void)
{
    return bWifiModule.linked;
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

/**
 * \}
 */
#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
