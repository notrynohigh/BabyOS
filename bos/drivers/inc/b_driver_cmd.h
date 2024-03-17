/**
 *!
 * \file        b_driver_cmd.h
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
#ifndef __B_DRIVER_CMD_H__
#define __B_DRIVER_CMD_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

///////////////////////////////////////////////////////////
// eeprom Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_LTC_GET_DACX_STATUS 0  // uint32_t 获取某个DAC输出状态
#define bCMD_LTC_SET_CURRENT 1      // uint32_t
#define bCMD_LTC_GET_CURRENT 2      // uint32_t
#define bCMD_LTC_EXEC_DACX 3        // uint32_t 某个DAC按照私有参数执行
#define bCMD_LTC_STOP_DACX 4        // uint32_t 某个DAC取消输出

///////////////////////////////////////////////////////////
// 485 Command & Data Structure
///////////////////////////////////////////////////////////
typedef void (*bRS485Callback_t)(uint8_t *pbuf, uint16_t len);
#define bCMD_485_REG_CALLBACK 0  // bRS485Callback_t
#define bCMD_485_IDLE_MS 1       // uint16_t
///////////////////////////////////////////////////////////
// eeprom Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_EE_SET_CAPACITY 0  // uint32_t
#define bCMD_EE_GET_CAPACITY 1  // uint32_t
#define bCMD_EE_PAGE_SIZE 2     // uint32_t

///////////////////////////////////////////////////////////
// Flash Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_ERASE_SECTOR 0      // bFlashErase_t
#define bCMD_GET_SECTOR_SIZE 1   // uint32_t
#define bCMD_GET_SECTOR_COUNT 2  // uint32_t

typedef struct
{
    uint32_t addr;
    uint32_t num;
} bFlashErase_t;

///////////////////////////////////////////////////////////
// GSensor Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_CFG_ODR 0        // b_drv_xxx.h odr struct
#define bCMD_CFG_FS 1         // b_drv_xxx.h fs struct
#define bCMD_CFG_FIFO 2       // bGSensorFifo_t
#define bCMD_CFG_POWERDOWN 3  // none

typedef struct
{
    int16_t x_mg;
    int16_t y_mg;
    int16_t z_mg;
} bGsensor3Axis_t;

typedef struct
{
    uint8_t fifo_en;
    uint8_t fifo_mode;
    uint8_t fifo_length;
} bGSensorFifo_t;

typedef struct
{
    int32_t acc_arr[3];

    int32_t gyro_arr[3];

    int32_t mag_arr[3];

    float temperature;

} bICM20948_9Axis_t;
#define bCMD_ICM20948_SET_STATUS_ERR 0

typedef struct
{
    float temperature;

    int16_t acc_arr[3];

    int16_t gyro_arr[3];
} bICM42688P_6Axis_t;
#define bCMD_ICM42688P_SET_STATUS_ERR 0

typedef struct
{
    float mag_arr[3];
} bQMC5883L_3Axis_t;
#define bCMD_QMC5883L_WHETHER_NEWDATA_READY 0
#define bCMD_QMC5883L_SET_STATUS_ERR 1

typedef struct
{
    float acc_arr[3];  // m/s^2

    float gyro_arr[3];  // rad/s
} bQMI8658A_6Axis_t;
#define bCMD_QMI8658A_SET_STATUS_ERR 0

///////////////////////////////////////////////////////////
// LCD  Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_FILL_RECT 0  // bLcdRectInfo_t
#define bCMD_FILL_BMP 1   // bLcdBmpInfo_t
#define bCMD_SET_SIZE 2   // bLcdSize_t

typedef struct
{
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint16_t color;
} bLcdRectInfo_t;

typedef struct
{
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint8_t *color;
} bLcdBmpInfo_t;

typedef struct
{
    uint16_t color;
} bLcdWrite_t;

typedef struct
{
    uint16_t reg;
    uint16_t dat;
} bLcdRWAddress_t;

typedef struct
{
    uint16_t width;
    uint16_t length;
} bLcdSize_t;

///////////////////////////////////////////////////////////
// Touch Command & Data Structure
///////////////////////////////////////////////////////////
typedef struct
{
    uint16_t x_ad;
    uint16_t y_ad;
} bTouchAdVal_t;

///////////////////////////////////////////////////////////
// Temperature Command & Data Structure
///////////////////////////////////////////////////////////
typedef struct
{
    int16_t tempx100;
} bTempVal_t;

///////////////////////////////////////////////////////////
// Sensor Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_SENSOR_START 0

///////////////////////////////////////////////////////////
// Wifi Module Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_WIFI_REG_CALLBACK 0       // bWifiDrvCallback_t
#define bCMD_WIFI_MODE_STA 1           // none
#define bCMD_WIFI_MODE_AP 2            // bApInfo_t
#define bCMD_WIFI_MODE_STA_AP 3        // bApInfo_t
#define bCMD_WIFI_JOIN_AP 4            // bApInfo_t
#define bCMD_WIFI_PING 5               // char *
#define bCMD_WIFI_LOCAL_TCP_SERVER 6   // bTcpUdpInfo_t
#define bCMD_WIFI_LOCAL_UDP_SERVER 7   // bTcpUdpInfo_t
#define bCMD_WIFI_REMOT_TCP_SERVER 8   // bTcpUdpInfo_t
#define bCMD_WIFI_REMOT_UDP_SERVER 9   // bTcpUdpInfo_t
#define bCMD_WIFI_TCPUDP_CLOSE 10      // bTcpUdpInfo_t
#define bCMD_WIFI_TCPUDP_SEND 11       // bTcpUdpData_t
#define bCMD_WIFI_MQTT_CONN 12         // bMqttConnInfo_t
#define bCMD_WIFI_MQTT_SUB 13          // bMqttTopic_t
#define bCMD_WIFI_MQTT_PUB 14          // bMqttData_t
#define bCMD_WIFI_SET_CALLBACK_ARG 15  // void *
typedef enum
{
    B_EVT_MODE_STA_OK = 0,
    B_EVT_MODE_AP_OK,
    B_EVT_MODE_STA_AP_OK,
    B_EVT_JOIN_AP_OK,
    B_EVT_PING_OK,
    B_EVT_LOCAL_TCP_SERVER_OK,
    B_EVT_LOCAL_UDP_SERVER_OK,
    B_EVT_CONN_TCP_SERVER_OK,
    B_EVT_CONN_UDP_SERVER_OK,
    B_EVT_CLOSE_CONN_OK,
    B_EVT_CONN_SEND_OK,
    B_EVT_CONN_NEW_DATA,

    B_EVT_FAIL_BASE = -100,
    B_EVT_MODE_STA_FAIL,
    B_EVT_MODE_AP_FAIL,
    B_EVT_MODE_STA_AP_FAIL,
    B_EVT_JOIN_AP_FAIL,
    B_EVT_PING_FAIL,
    B_EVT_LOCAL_TCP_SERVER_FAIL,
    B_EVT_LOCAL_UDP_SERVER_FAIL,
    B_EVT_CONN_TCP_SERVER_FAIL,
    B_EVT_CONN_UDP_SERVER_FAIL,
    B_EVT_CLOSE_CONN_FAIL,
    B_EVT_CONN_SEND_FAIL,
} bWifiDrvEvent_t;

typedef struct
{
    void (*cb)(bWifiDrvEvent_t event, void *arg, void (*release)(void *), void *user_data);
    void *user_data;
} bWifiDrvCallback_t;

#define WIFI_SSID_LEN_MAX (32)
#define WIFI_PASSWD_LEN_MAX (64)
#define WIFI_REMOTE_ADDR_LEN_MAX (64)
typedef struct
{
    char    ssid[WIFI_SSID_LEN_MAX + 1];
    char    passwd[WIFI_PASSWD_LEN_MAX + 1];
    uint8_t encryption;
    // 0(open) 1(WPA_PSK) 2(WPA2_PSK) 3(WPA_WPA2_PSK)
} bApInfo_t;

typedef struct
{
    char     ip[WIFI_REMOTE_ADDR_LEN_MAX + 1];
    uint16_t port;
} bTcpUdpInfo_t;

typedef struct
{
    bTcpUdpInfo_t conn;
    uint8_t      *pbuf;
    uint16_t      len;
    void (*release)(void *);
} bTcpUdpData_t;

typedef struct
{
    char     broker[64];
    uint16_t port;
    char     device_id[64];
    char     user[64];
    char     passwd[64];
} bMqttConnInfo_t;

typedef struct
{
    char    topic[64];
    uint8_t qos;
} bMqttTopic_t;

typedef struct
{
    bMqttTopic_t topic;
    uint8_t     *pbuf;
    uint16_t     len;
    void (*release)(void *);
} bMqttData_t;

///////////////////////////////////////////////////////////
// Proximity_AmbientLightsensor Data Structure
///////////////////////////////////////////////////////////
typedef struct
{
    int16_t ALS_Channel_0;
    int16_t ALS_Channel_1;
    int16_t Prox_data;
} bProximity_AmbientLightsensor_t;

///////////////////////////////////////////////////////////
// MAC Device Command and Param
///////////////////////////////////////////////////////////
#define bCMD_MAC_ADDRESS 0        // bMacAddress_t
#define bCMD_GET_LINK_STATE 1     // uint8_t  0 or 1 (linked)
#define bCMD_LINK_STATE_CHANGE 2  // uint8_t  0 or 1 (linked)
#define bCMD_REG_BUF_LIST 3       // bHalBufList_t

typedef struct
{
    uint8_t address[6];
} bMacAddress_t;

///////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
