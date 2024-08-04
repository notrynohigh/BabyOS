/**
 *!
 * \file        b_srv_mqtt.c
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
#include "services/inc/b_srv_mqtt.h"

#if (defined(_MQTT_SERVICE_ENABLE) && (_MQTT_SERVICE_ENABLE == 1))

#include "core/inc/b_task.h"
#include "core/inc/b_timer.h"
#include "thirdparty/mqtt-pack/MQTTPacket.h"
#include "utils/inc/b_util_list.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup MQTT
 * \{
 */

/**
 * \defgroup MQTT_Private_TypesDefinitions
 * \{
 */
typedef struct
{
    uint8_t          stat;
    char            *pbroker;
    char            *host;
    uint8_t          is_mqtts;
    uint16_t         port;
    char            *client_id;
    char            *user_name;
    char            *user_passwd;
    uint16_t         keep_alive;
    pbMqttCallback_t cb;
    void            *user_data;
    bTaskId_t        task_id;
    bTimerId_t       timer_id;
    int              sock_fd;
    uint32_t         last_recv;
    uint16_t         packet_id;
} bMqttSrvInstance_t;

typedef struct
{
    uint8_t  type;
    uint8_t *pack;
    uint32_t pack_len;
    void (*release)(void *);
} bMqttPack_t;

typedef struct
{
    uint8_t          state;
    uint16_t         pack_id;
    uint16_t         count;
    char            *pack;
    uint32_t         pack_len;
    struct list_head node;
} bMqttSubscribeNode_t;

/**
 * \}
 */

/**
 * \defgroup MQTT_Private_Defines
 * \{
 */
#define B_MQTT_STA_INIT (0)
#define B_MQTT_STA_TCP_CONNECTED (1)

#define MQTT_STEP_CONN (0x1)
#define MQTT_STEP_SUB (0x2)

#define MAX_PACKET_ID 65535
/**
 * \}
 */

/**
 * \defgroup MQTT_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MQTT_Private_Variables
 * \{
 */
static bMqttSrvInstance_t *pbMqttInstance = NULL;
static LIST_HEAD(bMqttSubscribeListHead);

B_TASK_CREATE_ATTR(bMqttTaskAttr);
B_TIMER_CREATE_ATTR(bMqttTimerAttr);
/**
 * \}
 */

/**
 * \defgroup MQTT_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MQTT_Private_Functions
 * \{
 */

static int _bMqttParseUrl(const char *url, char **host, uint16_t *port, uint8_t *is_mqtts)
{
    // Check if the URL starts with "mqtt://" or "mqtts://"
    const char *prefix_mqtt    = "mqtt://";
    const char *prefix_mqtts   = "mqtts://";
    const char *hostname_start = NULL;
    int         tmp_port       = -1;
    uint8_t     tmp_is_mqtts   = 0;
    if (strncmp(url, prefix_mqtt, strlen(prefix_mqtt)) == 0)
    {
        tmp_is_mqtts   = 0;
        hostname_start = url + strlen(prefix_mqtt);
    }
    else if (strncmp(url, prefix_mqtts, strlen(prefix_mqtts)) == 0)
    {
        tmp_is_mqtts   = 1;
        hostname_start = url + strlen(prefix_mqtts);
    }
    else
    {
        return -1;  // Invalid URL scheme
    }

    // Find the position of the colon (:) and slash (/) after the hostname
    const char *colon_pos = strchr(hostname_start, ':');
    const char *slash_pos = strchr(hostname_start, '/');

    // Extract the hostname
    uint32_t hostname_len = 0;
    if (colon_pos != NULL)
    {
        hostname_len = colon_pos - hostname_start;
    }
    else if (slash_pos != NULL)
    {
        hostname_len = slash_pos - hostname_start;
    }
    else
    {
        hostname_len = strlen(hostname_start);
    }

    if (hostname_len > strlen(hostname_start))
    {
        return -1;
    }

    char *phostname = bMalloc(hostname_len + 1);
    if (phostname == NULL)
    {
        return -1;
    }

    strncpy(phostname, hostname_start, hostname_len);
    phostname[hostname_len] = '\0';

    // Extract the port if specified
    if (colon_pos != NULL)
    {
        const char *port_start  = colon_pos + 1;
        char        port_str[6] = {0};  // Port number is at most 5 digits
        uint32_t    port_len    = 0;

        if (slash_pos != NULL)
        {
            port_len = slash_pos - port_start;
        }
        else
        {
            port_len = strlen(port_start);
        }

        if (port_len >= sizeof(port_str))
        {
            bFree(phostname);
            return -1;  // Port number too long
        }

        strncpy(port_str, port_start, port_len);
        tmp_port = atoi(port_str);
    }

    // If port is not specified, set default ports
    if (tmp_port == -1)
    {
        tmp_port = tmp_is_mqtts ? 8883 : 1883;
    }

    *host     = phostname;
    *port     = (uint16_t)(tmp_port & 0xffff);
    *is_mqtts = tmp_is_mqtts;
    return 0;
}

static int _bMqttCalBuffSize(MQTTPacket_connectData *data)
{
    int len = 0;
    // 固定报头
    len += 2;
    // 可变报头
    len += 2 + strlen("MQTT");  // 协议名长度和协议名
    len += 1;                   // 协议级别
    len += 1;                   // 连接标志
    len += 2;                   // 保持连接时间
    // 有效载荷
    len += 2 + strlen(data->clientID.cstring);  // 客户端标识符长度和客户端标识符
    if (data->willFlag)
    {
        len += 2 + strlen(data->will.topicName.cstring);  // 遗嘱主题长度和遗嘱主题
        len += 2 + data->will.message.lenstring.len;      // 遗嘱消息长度和遗嘱消息
    }
    if (data->username.cstring)
    {
        len += 2 + strlen(data->username.cstring);  // 用户名长度和用户名
    }
    if (data->password.cstring)
    {
        len += 2 + strlen(data->password.cstring);  // 密码长度和密码
    }
    return len;
}

static void _bMqttTransCb(bTransEvent_t event, void *param, void *arg)
{
    if ((event == B_TRANS_DISCONNECT) && (pbMqttInstance->stat == B_MQTT_STA_TCP_CONNECTED))
    {
        pbMqttInstance->stat = B_MQTT_STA_INIT;
    }
}

static int _bMqttRead(bMqttSrvInstance_t *pinstance, uint8_t *pbuf, uint16_t len)
{
    uint16_t rlen = 0;
    int      ret  = 0;
    if (pinstance->is_mqtts == 0)
    {
        ret = bRecv(pinstance->sock_fd, pbuf, len, &rlen);
        if (ret < 0)
        {
            return ret;
        }
    }
    else
    {
    }
    return rlen;
}

static int _bMqttWrite(bMqttSrvInstance_t *pinstance, uint8_t *pbuf, uint16_t len)
{
    uint16_t rlen = 0;
    int      ret  = 0;
    if (pinstance->is_mqtts == 0)
    {
        ret = bSend(pinstance->sock_fd, pbuf, len, &rlen);
        if (ret < 0)
        {
            return ret;
        }
    }
    else
    {
    }
    return rlen;
}

static int _bMqttDecodePacket(bMqttSrvInstance_t *pinstance, int *value)
{
    unsigned char i;
    int           multiplier                       = 1;
    int           len                              = 0;
    const int     MAX_NO_OF_REMAINING_LENGTH_BYTES = 4;

    *value = 0;
    do
    {
        int rc = MQTTPACKET_READ_ERROR;
        if (++len > MAX_NO_OF_REMAINING_LENGTH_BYTES)
        {
            rc  = MQTTPACKET_READ_ERROR; /* bad data */
            len = 0;
            b_log_e("MQTT read remaining length error\r\n");
            goto exit;
        }
        rc = _bMqttRead(pinstance, &i, 1);
        if (rc != 1)
        {
            len = 0;
            b_log_e("bMqttRead failed\r\n");
            goto exit;
        }
        *value += (i & 127) * multiplier;
        multiplier *= 128;
    } while ((i & 128) != 0);
exit:
    return len;
}

static int _bMqttConnect(bMqttSrvInstance_t *pinstance)
{
    MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
    int                    len     = 0;

    options.clientID.cstring = pinstance->client_id;
    options.username.cstring = pinstance->user_name;
    options.password.cstring = pinstance->user_passwd;

    len = _bMqttCalBuffSize(&options);
    if (len <= 0)
    {
        return -1;
    }

    uint8_t *pbuf = bMalloc(len);
    if (pbuf == NULL)
    {
        return -1;
    }

    if ((len = MQTTSerialize_connect(pbuf, len, &options)) <= 0)
    {
        bFree(pbuf);
        pbuf = NULL;
        return -1;
    }
    _bMqttWrite(pinstance, pbuf, len);
    bFree(pbuf);
    pbuf = NULL;
    return 0;
}

static uint16_t _bMqttGetNextPacketId(bMqttSrvInstance_t *pinstance)
{
    return pinstance->packet_id =
               (pinstance->packet_id == MAX_PACKET_ID) ? 1 : pinstance->packet_id + 1;
}

static int _bMqttSubscribeReset()
{
    bMqttSubscribeNode_t *pnode = NULL;
    struct list_head     *pos   = NULL;
    list_for_each(pos, &bMqttSubscribeListHead)
    {
        pnode        = list_entry(pos, bMqttSubscribeNode_t, node);
        pnode->state = 0;
    }
    return 0;
}

static int _bMqttSubscribe(bMqttSrvInstance_t *pinstance)
{
    bMqttSubscribeNode_t *pnode = NULL;
    struct list_head     *pos   = NULL;
    list_for_each(pos, &bMqttSubscribeListHead)
    {
        pnode = list_entry(pos, bMqttSubscribeNode_t, node);
        if (pnode->state == 0)
        {
            b_log("sub:%d\r\n", pnode->pack_id);
            if (pnode->pack != NULL)
            {
                bSend(pinstance->sock_fd, (uint8_t *)pnode->pack, pnode->pack_len, NULL);
                break;
            }
        }
    }
    return 0;
}

static int _bMqttSubscribeAckHandle(bMqttPack_t *pack)
{
    int      index   = 1;
    uint16_t pack_id = 0;
    while (pack->pack[index] & 0x80)
    {
        index += 1;
    }
    index += 1;
    pack_id |= pack->pack[index];
    pack_id <<= 8;
    pack_id |= pack->pack[index + 1];
    b_log("suback:%d\r\n", pack_id);

    bMqttSubscribeNode_t *pnode = NULL;
    struct list_head     *pos   = NULL;
    list_for_each(pos, &bMqttSubscribeListHead)
    {
        pnode = list_entry(pos, bMqttSubscribeNode_t, node);
        if (pnode->pack_id == pack_id)
        {
            pnode->state = 1;
            return 0;
        }
    }
    return -1;
}

static int _bMqttAddSubscribe(bMqttSrvInstance_t *pinstance, const char **topic, int *qos,
                              int topic_num)
{
    int         len       = 0;
    int         ret       = 0;
    int         i         = 0;
    MQTTString *topic_str = bMalloc(topic_num * sizeof(MQTTString));
    if (topic_str == NULL)
    {
        return -1;
    }
    len = 1 + 4 + 2;
    for (i = 0; i < topic_num; i++)
    {
        topic_str[i].cstring = (char *)topic[i];
        len += strlen(topic[i]) + 3;
    }
    b_log("sub malloc len:%d\r\n", len);
    uint8_t *pack = bMalloc(len);
    if (pack == NULL)
    {
        bFree(topic_str);
        return -1;
    }
    uint16_t pack_id = _bMqttGetNextPacketId(pinstance);
    len              = MQTTSerialize_subscribe(pack, len, 0, pack_id, topic_num, topic_str, qos);
    if (len <= 0)
    {
        b_log_w("serialize sub err..%d.\r\n", len);
        bFree(topic_str);
        bFree(pack);
        return -1;
    }
    bFree(topic_str);
    topic_str = NULL;

    bMqttSubscribeNode_t *pnode = bMalloc(sizeof(bMqttSubscribeNode_t));
    if (pnode == NULL)
    {
        bFree(pack);
        return -1;
    }
    pnode->state    = 0;
    pnode->count    = topic_num;
    pnode->pack_id  = pack_id;
    pnode->pack     = (char *)pack;
    pnode->pack_len = len;
    list_add(&pnode->node, &bMqttSubscribeListHead);
    return 0;
}

static int _bMqttReadPacket(bMqttSrvInstance_t *pinstance, bMqttPack_t *pack)
{
    MQTTHeader header     = {0};
    int        len        = 0;
    uint8_t    fix_byte   = 0;
    int        remain_len = 0;
    uint8_t    buf[8];
    memset(pack, 0, sizeof(bMqttPack_t));
    /* 1. read the header byte.  This has the packet type in it */
    int rc = _bMqttRead(pinstance, &fix_byte, 1);
    if (rc != 1)
    {
        b_log_e("read header byte failed");
        goto exit;
    }
    /* 2. read the remaining length.  This is variable in itself */
    if (0 == _bMqttDecodePacket(pinstance, &remain_len))
    {
        b_log_e("decode packet failed\r\n");
        goto exit;
    }
    /* put the original remaining length back into the buffer */
    len            = MQTTPacket_encode(buf, remain_len);
    pack->pack_len = remain_len + len + 1;
    b_log("pack length:%d\r\n", pack->pack_len);
    pack->pack = bMalloc(pack->pack_len);
    if (pack->pack == NULL)
    {
        b_log_e("mem error...\r\n");
        goto exit;
    }
    pack->release = bFree;
    pack->pack[0] = fix_byte;
    memcpy(pack->pack + 1, buf, len);
    /* 3. read the rest of the buffer using a callback to supply the rest of the data */
    if (remain_len > 0 &&
        (rc = _bMqttRead(pinstance, pack->pack + 1 + len, remain_len) != remain_len))
    {
        goto exit;
    }
    header.byte = fix_byte;
    pack->type  = header.bits.type;
    return 0;
exit:
    if (pack->pack)
    {
        bFree(pack->pack);
        pack->pack    = NULL;
        pack->release = NULL;
    }
    return -1;
}

static void _bMqttTimerCb(void *arg)
{
    bMqttSrvInstance_t *pinstance = (bMqttSrvInstance_t *)arg;
    uint8_t             buf[4];
    if (pinstance->stat != B_MQTT_STA_INIT)
    {
        int len = MQTTSerialize_pingreq(buf, sizeof(buf));
        if ((bHalGetSysTick() - pinstance->last_recv) > MS2TICKS(pinstance->keep_alive * 1000))
        {
            pbMqttInstance->stat = B_MQTT_STA_INIT;
        }
        else
        {
            bSend(pinstance->sock_fd, buf, len, NULL);
        }
    }
}

PT_THREAD(_bMqttTaskFunc)(struct pt *pt, void *arg)
{
    static int          sock_fd     = -1;
    static uint8_t      mqtt_step_f = 0;
    int                 pack_len    = 0;
    uint8_t            *pbuf        = NULL;
    uint32_t            buf_len     = 0;
    bMqttSrvInstance_t *pinstance   = (bMqttSrvInstance_t *)arg;
    bMqttPack_t         pack;
    bMqttEvent_t        evt = B_MQTT_EVT_INVALID;
    bMqttEvtParam_t     param;
    MQTTString          topic_name;
    B_TASK_INIT_BEGIN();
    // ...
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        if (pinstance == NULL)
        {
            bTaskRestart(pt);
        }

        if (pinstance->stat == B_MQTT_STA_INIT)
        {
            sock_fd = bSocket(B_TRANS_CONN_TCP, _bMqttTransCb, pinstance);
            if (SOCKFD_IS_INVALID(sock_fd))
            {
                bTaskRestart(pt);
            }
            b_log("connect:%s:%d\r\n", pinstance->host, pinstance->port);
            bConnect(sock_fd, pinstance->host, pinstance->port);
            PT_WAIT_UNTIL(pt, bSockIsWriteable(sock_fd), MS2TICKS(3000));
            if (PT_WAIT_IS_TIMEOUT(pt))
            {
                b_log_e("connect timeout\r\n");
                SOCKET_SHUTDOWN(pt, sock_fd);
                bTaskRestart(pt);
            }
            pinstance->stat    = B_MQTT_STA_TCP_CONNECTED;
            pinstance->sock_fd = sock_fd;
            mqtt_step_f        = 0;
            b_log("tcp connected success!\r\n");
        }
        else
        {
            PT_WAIT_UNTIL(
                pt, (bSockIsReadable(sock_fd) || (pinstance->stat != B_MQTT_STA_TCP_CONNECTED)),
                1000);
            if (pinstance->stat != B_MQTT_STA_TCP_CONNECTED)
            {
                b_log("tcp disconnect...\r\n");
                bTimerStop(pinstance->timer_id);
                _bMqttSubscribeReset();
                SOCKET_SHUTDOWN(pt, sock_fd);
                bTaskRestart(pt);
            }
            else if (bSockIsReadable(sock_fd))
            {
                if (0 == _bMqttReadPacket(pinstance, &pack))
                {
                    evt                  = B_MQTT_EVT_INVALID;
                    pinstance->last_recv = bHalGetSysTick();
                    b_log("packet type: %d\r\n", pack.type);
                    switch (pack.type)
                    {
                        case CONNACK:
                        {
                            mqtt_step_f |= MQTT_STEP_CONN;
                            evt = B_MQTT_EVT_CONN;
                            bTimerStart(pinstance->timer_id, pinstance->keep_alive * 1000 / 3);
                        }
                        break;
                        case PUBACK:
                        {
                            ;  // todo qos = 1
                        }
                        break;
                        case SUBACK:
                        {
                            _bMqttSubscribeAckHandle(&pack);
                        }
                        break;
                        case PUBLISH:
                        {
                            if (MQTTDeserialize_publish(
                                    &param.pub.dup, &param.pub.qos, &param.pub.retained,
                                    &param.pub.pack_id, &topic_name,
                                    (unsigned char **)&param.pub.payload,
                                    (int *)&param.pub.payload_len, pack.pack, pack.pack_len) == 1)
                            {
                                param.pub.topic     = topic_name.lenstring.data;
                                param.pub.topic_len = topic_name.lenstring.len;
                                evt                 = B_MQTT_EVT_PUB;
                            }
                            break;
                        }
                        case PUBREC:
                        case PUBREL:
                        {
                            // todo qos = 2
                            break;
                        }
                        case PUBCOMP:
                            break;
                        case PINGRESP:
                            break;
                    }
                    if (evt != B_MQTT_EVT_INVALID)
                    {
                        pinstance->cb(evt, &param, pinstance->user_data);
                    }
                    if (pack.release && pack.pack)
                    {
                        pack.release(pack.pack);
                        pack.pack = NULL;
                    }
                }
            }
            else
            {
                if ((mqtt_step_f & MQTT_STEP_CONN) == 0)
                {
                    _bMqttConnect(pinstance);
                }
                else
                {
                    _bMqttSubscribe(pinstance);
                }
            }
        }
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup MQTT_Exported_Functions
 * \{
 */
int bMqttSrvStartWithCfg(pbMqttCallback_t cb, void *arg)
{
    bMqttSrvInstance_t *pinstance = NULL;
    if (pbMqttInstance != NULL || cb == NULL)
    {
        return -1;
    }
    pinstance = bCalloc(1, sizeof(bMqttSrvInstance_t));
    if (pinstance == NULL)
    {
        return -1;
    }
    pinstance->cb          = cb;
    pinstance->user_data   = arg;
    pinstance->packet_id   = 1;
    pinstance->keep_alive  = MQTT_KEEP_ALIVE;
    pinstance->pbroker     = bStrDup(MQTT_BROKER_URL);
    pinstance->client_id   = bStrDup(MQTT_CLIENT_ID);
    pinstance->user_name   = bStrDup(MQTT_USER_NAME);
    pinstance->user_passwd = bStrDup(MQTT_USER_PASSWD);
    if (IS_NULL(pinstance->pbroker) || IS_NULL(pinstance->user_name) ||
        IS_NULL(pinstance->user_passwd) || IS_NULL(pinstance->client_id))
    {
        goto fail;
    }
    int ret = _bMqttParseUrl(pinstance->pbroker, &pinstance->host, &pinstance->port,
                             &pinstance->is_mqtts);
    if (ret < 0)
    {
        goto fail;
    }
    pinstance->task_id = bTaskCreate("mqtt", _bMqttTaskFunc, pinstance, &bMqttTaskAttr);
    if (pinstance->task_id <= 0)
    {
        goto fail;
    }
    pinstance->timer_id = bTimerCreate(_bMqttTimerCb, B_TIMER_PERIODIC, pinstance, &bMqttTimerAttr);
#ifdef MQTT_SUB_TOPIC_DEFAULT
    char *topic_filter = MQTT_SUB_TOPIC_DEFAULT;
    int   qos          = MQTT_QOS_DEFAULT;
    if (0 != _bMqttAddSubscribe(pinstance, (const char **)&topic_filter, &qos, 1))
    {
        goto fail;
    }
#endif
    pinstance->stat = B_MQTT_STA_INIT;
    pbMqttInstance  = pinstance;
    return 0;
fail:
    if (pinstance->task_id > 0)
    {
        bTaskRemove(pinstance->task_id);
    }
    if (pinstance->timer_id > 0)
    {
        bTimerDelete(pinstance->timer_id);
    }
    if (pinstance->host)
    {
        bFree(pinstance->host);
    }
    if (pinstance->client_id)
    {
        bFree(pinstance->client_id);
    }
    if (pinstance->user_passwd)
    {
        bFree(pinstance->user_passwd);
    }
    if (pinstance->user_name)
    {
        bFree(pinstance->user_name);
    }
    if (pinstance->pbroker)
    {
        bFree(pinstance->pbroker);
    }
    return -1;
}

void bMqttSrvDestroy()
{
    bMqttSrvInstance_t *pinstance = pbMqttInstance;
    if (pinstance == NULL)
    {
        return;
    }
    if (pinstance->task_id > 0)
    {
        bTaskRemove(pinstance->task_id);
    }
    if (pinstance->timer_id > 0)
    {
        bTimerDelete(pinstance->timer_id);
    }
    if (pinstance->host)
    {
        bFree(pinstance->host);
    }
    if (pinstance->client_id)
    {
        bFree(pinstance->client_id);
    }
    if (pinstance->user_passwd)
    {
        bFree(pinstance->user_passwd);
    }
    if (pinstance->user_name)
    {
        bFree(pinstance->user_name);
    }
    if (pinstance->pbroker)
    {
        bFree(pinstance->pbroker);
    }
    bMqttSubscribeNode_t *pnode = NULL;
    struct list_head     *pos   = NULL;
    list_for_each(pos, &bMqttSubscribeListHead)
    {
        if (pnode != NULL)
        {
            bFree(pnode);
            pnode = NULL;
        }
        pnode = list_entry(pos, bMqttSubscribeNode_t, node);
        bFree(pnode->pack);
        pnode->pack = NULL;
        __list_del(pos->prev, pos->next);
    }
    if (pnode != NULL)
    {
        bFree(pnode);
        pnode = NULL;
    }
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
