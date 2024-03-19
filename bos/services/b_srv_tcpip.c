/**
 *!
 * \file        b_srv_tcpip.c
 * \version     v0.0.1
 * \date        2023/08/27
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 Bean
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
#include "services/inc/b_srv_tcpip.h"

#if (defined(_TCPIP_SERVICE_ENABLE) && (_TCPIP_SERVICE_ENABLE == 1))

#include <string.h>

#include "core/inc/b_sem.h"
#include "core/inc/b_task.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_utc.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup TCPIP
 * \{
 */

/**
 * \defgroup TCPIP_Private_TypesDefinitions
 * \{
 */

typedef struct
{
    uint32_t seconds;
    uint32_t fraction;
} bNtpTimestamp_t;

typedef struct
{
    uint8_t         li_vn_mode;       // Leap indicator, version number, and mode
    uint8_t         stratum;          // Stratum level of the local clock
    uint8_t         poll;             // Maximum interval between successive messages
    uint8_t         precision;        // Precision of the local clock
    uint32_t        root_delay;       // Total round trip delay time
    uint32_t        root_dispersion;  // Max error allowed from primary clock source
    uint32_t        ref_id;           // Reference clock identifier
    bNtpTimestamp_t ref_time;         // Reference timestamp
    bNtpTimestamp_t orig_time;        // Originate timestamp
    bNtpTimestamp_t recv_time;        // Receive timestamp
    bNtpTimestamp_t trans_time;       // Transmit timestamp
} bNtpPacket_t;

typedef struct
{
    bTaskId_t task_id;
    int       sockfd;
    uint32_t  interval_s;
} bNtpPcb_t;

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Defines
 * \{
 */
// NTP时间的起始时间
#define B_NTP_TIMESTAMP_DELTA 2208988800ull
#define B_NTP_SERVER_NUM (3)
#define B_NTP_TIMEOUT_S (20)
/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Variables
 * \{
 */
static bNtpPcb_t bNtpPcb = {
    .task_id = 0,
};
B_TASK_CREATE_ATTR(bNtpTask);
static const char *bNtpServer[B_NTP_SERVER_NUM] = {_NTP_SERVER_1, _NTP_SERVER_2, _NTP_SERVER_3};
/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Functions
 * \{
 */

static void _bNtpConnCallback(bTransEvent_t event, void *param, void *arg)
{
    ;
}

PT_THREAD(_bNtpTaskFunc)(struct pt *pt, void *arg)
{
    static uint8_t ntp_server_index = 0;
    bNtpPacket_t   packet;
    uint16_t       rlen     = 0;
    uint64_t       ntp_time = 0;
    PT_BEGIN(pt);
    while (1)
    {
        bNtpPcb.sockfd = bSocket(B_TRANS_CONN_UDP, _bNtpConnCallback, NULL);
        if (bNtpPcb.sockfd < 0)
        {
            b_log_e("socket fail...\r\n");
            break;
        }
        b_log("sockfd: %x %d\r\n", bNtpPcb.sockfd, ntp_server_index);
        bConnect(bNtpPcb.sockfd, (char *)bNtpServer[ntp_server_index], 123);
        PT_WAIT_UNTIL(pt, bSockIsWriteable(bNtpPcb.sockfd) == 1, B_NTP_TIMEOUT_S * 1000);
        if (pt->retval == PT_RETVAL_TIMEOUT)
        {
            b_log_e("ntp send fail...\r\n");
            PT_WAIT_UNTIL_FOREVER(pt, bShutdown(bNtpPcb.sockfd) >= 0);
            b_log_e("shutdown..\r\n");
            ntp_server_index = (ntp_server_index + 1) % B_NTP_SERVER_NUM;
            break;
        }
        memset(&packet, 0, sizeof(bNtpPacket_t));
        packet.li_vn_mode = 0x1b;
        bSend(bNtpPcb.sockfd, (uint8_t *)&packet, sizeof(bNtpPacket_t), NULL);
        PT_WAIT_UNTIL(pt, bSockIsReadable(bNtpPcb.sockfd) == 1, B_NTP_TIMEOUT_S * 1000);
        if (pt->retval == PT_RETVAL_TIMEOUT)
        {
            b_log_e("ntp recv timeout.. \r\n");
            PT_WAIT_UNTIL_FOREVER(pt, bShutdown(bNtpPcb.sockfd) >= 0);
            b_log_e("shutdown..\r\n");
            ntp_server_index = (ntp_server_index + 1) % B_NTP_SERVER_NUM;
            bTaskDelayMs(pt, 10000);
            break;
        }
        bRecv(bNtpPcb.sockfd, (uint8_t *)&packet, sizeof(bNtpPacket_t), &rlen);
        if (rlen == sizeof(bNtpPacket_t))
        {
            if (packet.recv_time.seconds <= packet.trans_time.seconds &&
                (packet.trans_time.seconds - packet.recv_time.seconds) <= 1)
            {
                ntp_time = B_SWAP_32(packet.trans_time.seconds) - B_NTP_TIMESTAMP_DELTA;
                bUTC_SetTime(ntp_time);
            }
        }
        PT_WAIT_UNTIL_FOREVER(pt, bShutdown(bNtpPcb.sockfd) >= 0);
        b_log_e("shutdown..\r\n");
        bTaskDelayMs(pt, bNtpPcb.interval_s * 1000);
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup TCPIP_Exported_Functions
 * \{
 */

int bTcpipSrvInit()
{
    return bNetlinkInit();
}

int bSntpStart(uint32_t interval_s)
{
    if (bNtpPcb.task_id == NULL)
    {
        memset(&bNtpPcb, 0, sizeof(bNtpPcb));
        bNtpPcb.sockfd     = -1;
        bNtpPcb.interval_s = 60 * 60;
        bNtpPcb.task_id    = bTaskCreate("ntp", _bNtpTaskFunc, NULL, &bNtpTask);
    }
    if (interval_s == 0)
    {
        return -1;
    }
    bNtpPcb.interval_s = interval_s;
    return 0;
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

/************************ Copyright (c) 2023 Bean *****END OF FILE****/
