/**
 *!
 * \file        b_srv_ntp.c
 * \version     v0.0.1
 * \date        2026/05/31
 * \author      BabyOS Team
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 BabyOS
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "services/inc/b_srv_ntp.h"

#if (defined(_NTP_SERVICE_ENABLE) && (_NTP_SERVICE_ENABLE == 1))

#include <stdio.h>
#include <string.h>

#include "core/inc/b_task.h"
#include "modules/inc/b_mod_tcpip.h"
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
 * \addtogroup NTP
 * \{
 */

/**
 * \defgroup NTP_Private_TypesDefinitions
 * \{
 */

typedef struct
{
    uint8_t seconds[4];
    uint8_t fraction[4];
} bNtpTimestamp_t;

typedef struct
{
    uint8_t         li_vn_mode;
    uint8_t         stratum;
    uint8_t         poll;
    uint8_t         precision;
    uint32_t        root_delay;
    uint32_t        root_dispersion;
    uint32_t        ref_id;
    bNtpTimestamp_t ref_time;
    bNtpTimestamp_t orig_time;
    bNtpTimestamp_t recv_time;
    bNtpTimestamp_t trans_time;
} bNtpPacket_t;

typedef struct
{
    bTaskId_t   task_id;
    bSocketFd_t sockfd;
    uint32_t    interval_s;
    uint8_t     running;
} bNtpPcb_t;

/**
 * \}
 */

/**
 * \defgroup NTP_Private_Defines
 * \{
 */

#define B_NTP_TIMESTAMP_DELTA 2208988800ull
#define B_NTP_SERVER_NUM 3
#define B_NTP_TIMEOUT_S 20

#ifndef _NTP_SERVER_1
#define _NTP_SERVER_1 "ntp1.aliyun.com"
#endif
#ifndef _NTP_SERVER_2
#define _NTP_SERVER_2 "ntp2.aliyun.com"
#endif
#ifndef _NTP_SERVER_3
#define _NTP_SERVER_3 "ntp3.aliyun.com"
#endif

/**
 * \}
 */

/**
 * \defgroup NTP_Private_Variables
 * \{
 */

static bNtpPcb_t s_ntp = {
    .task_id    = NULL,
    .sockfd     = -1,
    .interval_s = 3600,
    .running    = 0,
};

// H-NEW-4 fix: 标记 bSntpStop 调用过, 让 _bNtpTask 在 PT 顶层检测到后退出.
// 与 running 配合: running=0 是 task 自检, stop_requested 是外部信号.
static volatile uint8_t s_ntp_stop_requested = 0;
// M-NEW-5 fix: 统计连续 socket-alloc 失败次数, 阈值后降级为 b_log 而非 b_log_w,
// 避免没注册 netif 时日志风暴.
static uint8_t s_ntp_alloc_fail_count = 0;

B_TASK_CREATE_ATTR(bNtpTask);
static const char *s_ntp_server[B_NTP_SERVER_NUM] = {_NTP_SERVER_1, _NTP_SERVER_2, _NTP_SERVER_3};

// C8 fix: socket fd 释放同步. 没有公开 close API, 只能等 stack 在 B_TRANS_DISCONNECT 后释放.
// _bNtpConnCallback 置位, _bNtpTask 在 shutdown 完成后检测此位再清 s_ntp.sockfd.
static volatile uint8_t s_ntp_disconnected = 0;

/**
 * \}
 */

/**
 * \defgroup NTP_Private_Functions
 * \{
 */

static void _bNtpConnCallback(bTransEvent_t event, void *param, void *arg)
{
    (void)param;
    (void)arg;
    if (event == B_TRANS_DISCONNECT)
    {
        // 标记 socket 已断开. _bNtpTask 看到此位后才把 s_ntp.sockfd 置 -1,
        // 避免 fd 还在被 stack 内部使用时被覆盖 (C8 fix).
        s_ntp_disconnected = 1;
    }
    // 其他事件 (NEW_DATA, ERROR 等) NTP 路径用轮询处理, 不需要 callback
}

PT_THREAD(_bNtpTask)(struct pt *pt, void *arg)
{
    static uint8_t      server_index = 0;
    static bNtpPacket_t packet;
    static uint16_t     rlen      = 0;
    static uint64_t     ntp_time  = 0;
    static uint32_t     wait_time = 0;
    static int          send_ret  = 0;
    static int          recv_ret  = 0;
    static int          conn_ret  = 0;
    static int          shut_ret  = 0;

    (void)arg;
    wait_time = 10;  // 初始等待10秒

    PT_BEGIN(pt);

    // H-NEW-4 fix: 顶层 while 同时检测 running 和 stop_requested, 任一被清就退出.
    while (s_ntp.running && !s_ntp_stop_requested)
    {
        if (SOCKFD_IS_INVALID(s_ntp.sockfd))
        {
            // 创建UDP socket
            s_ntp.sockfd = bSocket(B_TRANS_CONN_UDP, _bNtpConnCallback, NULL);
            if (SOCKFD_IS_INVALID(s_ntp.sockfd))
            {
                // C7 fix: 不要紧 spin, 加 backoff (5s) 避免无 netif 时 100% CPU.
                // M-NEW-5 fix: 阈值后降级为 b_log, 避免 netif 长时间未注册时日志刷屏.
                s_ntp_alloc_fail_count++;
                if (s_ntp_alloc_fail_count < 10)
                {
                    b_log_w("[NTP] socket alloc fail #%u, retry in 5s\r\n",
                            (unsigned)s_ntp_alloc_fail_count);
                }
                else if (s_ntp_alloc_fail_count == 10)
                {
                    b_log("[NTP] socket alloc still failing, downgrade log to info level\r\n");
                }
                PT_WAIT_UNTIL(pt, s_ntp_stop_requested, 5000);  // FU-3: stop 打断长 sleep
                continue;
            }
            s_ntp_alloc_fail_count = 0;  // 成功 alloc, 计数清零
            s_ntp_disconnected = 0;   // C8 fix: 重置标志, 标记新 socket 生命周期开始
            b_log("[NTP] sockfd: %lx, server: %s\r\n", (unsigned long)s_ntp.sockfd,
                  s_ntp_server[server_index]);
        }
        else
        {
            // 检查连接状态
            if (bSocketIsConnected(s_ntp.sockfd))
            {
                // 已连接，检查是否可写
                if (bSockIsWriteable(s_ntp.sockfd))
                {
                    // 发送NTP请求
                    memset(&packet, 0, sizeof(packet));
                    packet.li_vn_mode = 0x1b;

                    // H-NEW-3 fix: 检查 bSend 返回值, 失败时直接跳到 shutdown 流程.
                    send_ret = bSend(s_ntp.sockfd, (uint8_t *)&packet, sizeof(packet), NULL);
                    // HIGH-NTP-1 fix: 之前 `<= 0` 把 ret==0 (合法, 表示写入 0 字节)
                    // 也判为失败. bSend 协议返回 0 不一定是错 (取决于实现), 应该只
                    // 检查 ret<0 视为错误.
                    if (send_ret < 0)
                    {
                        b_log_e("[NTP] send fail: %d\r\n", send_ret);
                    }
                    else
                    {
                        // 等待响应（使用PT_WAIT_UNTIL带超时）
                        // FU-3: 谓词加入 s_ntp_stop_requested, 让 bSntpStop 能立刻打断 recv 等待.
                        PT_WAIT_UNTIL(pt,
                                      bSockIsReadable(s_ntp.sockfd) == 1 || s_ntp_stop_requested,
                                      B_NTP_TIMEOUT_S * 1000);

                        if (PT_WAIT_IS_TIMEOUT(pt))
                        {
                            b_log_e("[NTP] recv timeout...\r\n");
                        }
                        else
                        {
                            // 接收数据
                            rlen = 0;
                            // H-NEW-3 fix: 检查 bRecv 返回值
                            recv_ret = bRecv(s_ntp.sockfd, (uint8_t *)&packet, sizeof(packet),
                                             &rlen);
                            if (recv_ret < 0)
                            {
                                b_log_e("[NTP] recv fail: %d\r\n", recv_ret);
                            }
                            else if (rlen == sizeof(packet))
                            {
                                // C6 fix: 解析时间时处理 NTP era 翻转 (post-2036-02-07).
                                // 原始代码只对比 packet.recv_time.seconds[0] 单字节 (无意义)
                                // 并直接 32 位减法 (era 1 时回卷). 此处改成:
                                //   1. 取 32 位 transmit timestamp, 大端转 host
                                //   2. 高位为 1 表示 era 1, OR 上 0xFFFFFFFF00000000 提升到 64 位
                                //   3. 减 NTP_TO_UNIX_DELTA 得 Unix 秒数
                                uint32_t *pseconds      = (uint32_t *)packet.trans_time.seconds;
                                uint32_t  trans_seconds = B_SWAP_32(*pseconds);
                                uint64_t  trans_full    = (uint64_t)trans_seconds;
                                if (trans_full & 0x80000000ULL)
                                {
                                    trans_full |= 0xFFFFFFFF00000000ULL;   // era 1
                                }
                                ntp_time = trans_full - B_NTP_TIMESTAMP_DELTA;
                                // M-NEW-3 fix: 显式 cast uint64_t -> bUTC_t (int64_t),
                                // 避免 -Wsign-conversion 警告, 值范围内转换明确定义.
                                bUTC_SetTime((bUTC_t)(int64_t)ntp_time);
                                b_log("[NTP] synced: %llu\r\n", (unsigned long long)ntp_time);
                                wait_time = s_ntp.interval_s;
                            }
                            else
                            {
                                b_log_w("[NTP] short recv: %u/%u\r\n",
                                        (unsigned)rlen, (unsigned)sizeof(packet));
                            }
                        }
                    }
                }

                // 关闭连接: C8 fix 用带超时的 wait (避免 FOREVER 死锁),
                // 等 s_ntp_disconnected 置位 (由 callback 异步触发) 再清 sockfd.
                // FU-3: 谓词加入 s_ntp_stop_requested, 让 stop 立刻打断 shutdown 等待.
                // CRIT-NTP-1 fix: 删除 `|| bShutdown(s_ntp.sockfd) >= 0` 子句.
                // bShutdown 已经调用过一次 (上面一行), 重复调用返回 -1, 子句永远
                // false. 这会让谓词永远只等 s_ntp_disconnected (callback) 或
                // s_ntp_stop_requested. callback 在 socket 真的断开时触发.
                shut_ret = bShutdown(s_ntp.sockfd);
                PT_WAIT_UNTIL(pt,
                              s_ntp_disconnected || s_ntp_stop_requested,
                              B_NTP_TIMEOUT_S * 1000);
                s_ntp.sockfd = -1;
                b_log("[NTP] shutdown (ret=%d), wait %us\r\n", shut_ret,
                      (unsigned)wait_time);

                // 等待间隔时间（使用protothread延时，不阻塞CPU）
                // FU-3: PT_WAIT_UNTIL 而非 PT_DELAY_MS, 让 stop 可立刻打断.
                PT_WAIT_UNTIL(pt, s_ntp_stop_requested, wait_time * 1000);
            }
            else
            {
                // 未连接，尝试连接
                // H-NEW-3 fix: 检查 bConnect 返回值
                conn_ret = bConnect(s_ntp.sockfd, (char *)s_ntp_server[server_index], 123);
                if (conn_ret < 0)
                {
                    b_log_e("[NTP] connect fail: %d, server=%s\r\n", conn_ret,
                            s_ntp_server[server_index]);
                    // CRIT-NTP-2 fix: bConnect 同步失败时 PCB 可能已被分配, 必须
                    // shutdown 触发 B_TRANS_DISCONNECT 回调, 等回调真的释放 PCB
                    // 才能 s_ntp.sockfd = -1. 之前直接清 -1 导致 PCB 永不释放,
                    // PCB 池耗尽后所有 DNS/NTP 失败.
                    bShutdown(s_ntp.sockfd);
                    PT_WAIT_UNTIL(pt,
                                  s_ntp_disconnected || s_ntp_stop_requested,
                                  B_NTP_TIMEOUT_S * 1000);
                    // 切换服务器
                    server_index = (server_index + 1) % B_NTP_SERVER_NUM;
                    s_ntp.sockfd = -1;
                    wait_time    = 60;
                    // FU-3: PT_WAIT_UNTIL 让 stop 立刻打断长 retry 等待.
                    PT_WAIT_UNTIL(pt, s_ntp_stop_requested, wait_time * 1000);
                    continue;
                }

                // 等待连接完成（带超时）
                // FU-3: 谓词加入 s_ntp_stop_requested, 让 stop 立刻打断连接等待.
                PT_WAIT_UNTIL(pt,
                              bSocketIsConnected(s_ntp.sockfd) == 1 || s_ntp_stop_requested,
                              B_NTP_TIMEOUT_S * 1000);

                if (PT_WAIT_IS_TIMEOUT(pt))
                {
                    b_log_e("[NTP] connect timeout...\r\n");
                    // C8 fix: 同上, 改成带超时的 wait
                    // FU-3: 谓词加入 s_ntp_stop_requested.
                    // CRIT-NTP-1 fix: 删除 `|| bShutdown(s_ntp.sockfd) >= 0` 子句
                    // (上一行 bShutdown 已调用, 重复调用返回 -1, 子句永远 false).
                    bShutdown(s_ntp.sockfd);
                    PT_WAIT_UNTIL(pt,
                                  s_ntp_disconnected || s_ntp_stop_requested,
                                  B_NTP_TIMEOUT_S * 1000);
                    b_log_e("[NTP] shutdown...\r\n");

                    // 切换到下一个服务器
                    server_index = (server_index + 1) % B_NTP_SERVER_NUM;
                    s_ntp.sockfd = -1;
                    wait_time    = 60;  // 失败后1分钟重试

                    // 等待1分钟（不阻塞CPU）
                    // FU-3: PT_WAIT_UNTIL 让 stop 立刻打断长 retry 等待.
                    PT_WAIT_UNTIL(pt, s_ntp_stop_requested, wait_time * 1000);
                }
            }
        }

        // 让出CPU（裸机必须让出，否则其他任务无法运行）
        bTaskYield(pt);
    }

    // H-NEW-4 fix: 退出前确保 socket 已关. 把 sockfd 重置前先 shutdown 触发 callback.
    if (!SOCKFD_IS_INVALID(s_ntp.sockfd))
    {
        bShutdown(s_ntp.sockfd);
        // FU-3: 谓词加入 s_ntp_stop_requested (task 已被外部标记停止).
        // CRIT-NTP-1 fix: 删除 `|| bShutdown(s_ntp.sockfd) >= 0` 子句.
        PT_WAIT_UNTIL(pt,
                      s_ntp_disconnected || s_ntp_stop_requested,
                      B_NTP_TIMEOUT_S * 1000);
        s_ntp.sockfd = -1;
    }
    // FU-3 fix: 之前只清 task_id 但没从 scheduler 摘除自己. PT_END 会 PT_INIT
    // 重置 protothread, _bTaskCore 不看 func 返回值, 下一轮又把 _bNtpTask
    // 拉起来从 PT_BEGIN 重跑, 形成 "task exited" 日志风暴 + s_ntp.running
    // 重新被 task 置 1 的诡异状态. 这里在 socket 已关闭后, 把 task 从 scheduler
    // 摘除 (enable=0, func=NULL), _bTaskCore 下一轮不再调用它. 与 b_srv_ota.c
    // 的 bTaskRemove 模式一致. socket 已关闭, 没有悬空 callback 风险.
    if (s_ntp.task_id != NULL) {
        bTaskRemove(s_ntp.task_id);
    }
    // REVIEW-V3 #9 fix: 清 task_id, 让 bSntpStart 后续能再创建 (不再有 race
    // 旧 task 还没退出就 bSntpStart → 双 task 并存).
    s_ntp.task_id = NULL;
    b_log("[NTP] task exited\r\n");

    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup NTP_Exported_Functions
 * \{
 */

int bSntpStart(uint32_t interval_s)
{
    if (s_ntp.running)
    {
        b_log_w("[NTP] already running\r\n");
        return 0;
    }
    // REVIEW-V3 #9 fix: 上一次 bSntpStop → bSntpStart 之间, 旧 task 可能还没退出
    // (task 自检后到 PT_END 还要若干 bExec() 周期). 此时 s_ntp.task_id != NULL, 拒绝
    // 再创建, 避免双 task 并存. _bNtpTask 的 PT_END 会清 task_id, 之后 bSntpStart
    // 才能成功.
    if (s_ntp.task_id != NULL)
    {
        b_log_w("[NTP] previous task still running, wait for cleanup\r\n");
        return -1;
    }

    if (interval_s == 0)
    {
        interval_s = 3600;
    }

    s_ntp.interval_s        = interval_s;
    s_ntp.running           = 1;
    s_ntp.sockfd            = -1;
    s_ntp_stop_requested    = 0;  // H-NEW-4: 每次 start 重置停止标志

    s_ntp.task_id = bTaskCreate("ntp", _bNtpTask, NULL, &bNtpTask);
    if (s_ntp.task_id == NULL)
    {
        s_ntp.running = 0;
        return -1;
    }

    b_log("[NTP] started, interval: %us\r\n", interval_s);
    return 0;
}

// H-NEW-4 fix: 提供公开 stop API, 让用户能在设备重启或重新配置时关掉 NTP task.
// 这里不能直接 bTaskRemove — task 内部还持着 socket, 强制移除会让 stack 里的
// callback 引用悬空. 改成置位 stop 标志 + 清 running, 让 task 在下次 PT 调度时
// 检测到并自检退出 (sockfd 也会被 shutdown).
int bSntpStop(void)
{
    if (!s_ntp.running)
    {
        return 0;  // 已停止, 幂等返 0
    }

    s_ntp_stop_requested = 1;
    s_ntp.running        = 0;
    b_log("[NTP] stop requested, cleanup happens in _bNtpTask (PT ctx)\r\n");
    // 调用方不阻塞等待, 内部资源会在若干个 bExec() 周期内释放完毕
    // (task 自检退出后会清 task_id, 下次 bSntpStart 可重新创建).
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

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
