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
// Base value of a certain type of general instruction
///////////////////////////////////////////////////////////
#define bCMD_TCPIP_GENERAL_BASE_VALUE (127)

///////////////////////////////////////////////////////////
// eeprom Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_LTC_GET_DACX_STATUS 0  // uint32_t 获取某个DAC输出状态
#define bCMD_LTC_SET_CURRENT 1      // uint32_t
#define bCMD_LTC_GET_CURRENT 2      // uint32_t
#define bCMD_LTC_EXEC_DACX 3        // uint32_t 某个DAC按照私有参数执行
#define bCMD_LTC_STOP_DACX 4        // uint32_t 某个DAC取消输出
#define bCMD_LTC_GET_DACX_FR 5      // uint32_t 获取Fault Register的FR0-FR4的值

///////////////////////////////////////////////////////////
// 485 Command & Data Structure
///////////////////////////////////////////////////////////
typedef void (*bRS485Callback_t)(uint8_t *pbuf, uint16_t len);
#define bCMD_485_REG_CALLBACK 0  // bRS485Callback_t
#define bCMD_485_IDLE_MS 1       // uint16_t
#define bCMD_485_SWITCH_DELAY 2  // (us) uint32_t
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
#define bCMD_GET_ID	3 			

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
    float mag_arr[3];
} bQMC5883P_3Axis_t;
#define bCMD_QMC5883P_WHETHER_NEWDATA_READY 0
#define bCMD_QMC5883P_SET_STATUS_ERR 1

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

typedef struct
{
    uint8_t humidity;
    int16_t tempx100;
} bTempHumidityVal_t;

///////////////////////////////////////////////////////////
// Sensor Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_SENSOR_START 0

///////////////////////////////////////////////////////////
// Wifi Module Command & Data Structure
///////////////////////////////////////////////////////////
#define bCMD_WIFI_REG_CALLBACK 0  // bWifiDrvCallback_t
#define bCMD_WIFI_MODE_STA 1      // none
#define bCMD_WIFI_MODE_AP 2       // bApInfo_t
#define bCMD_WIFI_MODE_STA_AP 3   // bApInfo_t
#define bCMD_WIFI_JOIN_AP 4       // bApInfo_t
#define bCMD_WIFI_NUMBER_MAX (5)

typedef enum
{
    B_EVT_MODE_STA_OK = 0,
    B_EVT_MODE_AP_OK,
    B_EVT_MODE_STA_AP_OK,
    B_EVT_JOIN_AP_OK,

    B_EVT_FAIL_BASE = -100,
    B_EVT_MODE_STA_FAIL,
    B_EVT_MODE_AP_FAIL,
    B_EVT_MODE_STA_AP_FAIL,
    B_EVT_JOIN_AP_FAIL,
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
#define bCMD_GET_DRIVER_NETIF (0 + bCMD_TCPIP_GENERAL_BASE_VALUE)   // bDriverNetif_t
#define bCMD_GET_MAC_ADDRESS (1 + bCMD_TCPIP_GENERAL_BASE_VALUE)    // bMacAddress_t
#define bCMD_SET_MAC_ADDRESS (2 + bCMD_TCPIP_GENERAL_BASE_VALUE)    // bMacAddress_t
#define bCMD_GET_LINK_STATE (3 + bCMD_TCPIP_GENERAL_BASE_VALUE)     // uint8_t  0 or 1 (linked)
#define bCMD_REG_LINK_CALLBACK (4 + bCMD_TCPIP_GENERAL_BASE_VALUE)  // bLinkStateCb_t
#define bCMD_REG_BUF_LIST (5 + bCMD_TCPIP_GENERAL_BASE_VALUE)       // bHalBufList_t
#define bCMD_GET_STACK_IF (6 + bCMD_TCPIP_GENERAL_BASE_VALUE)       // bTcpIpStackIf_t

typedef struct
{
    void *private;
} bDriverNetif_t;

typedef struct
{
    uint8_t address[6];
} bMacAddress_t;

typedef struct
{
    void (*cb)(uint8_t, void *);
    void *arg;
} bLinkStateCb_t;

typedef enum
{
    B_TCPIP_E_CONNECTING,
    B_TCPIP_E_CONNECTED,
    B_TCPIP_E_DISCONNECT,
    B_TCPIP_E_NEW_DATA,
    B_TCPIP_E_SEND_DONE,
} bTcpIpEvent_t;

typedef void (*pTcpIpCallback_t)(bTcpIpEvent_t event, void *pcb, void *arg);

typedef struct
{
    uint32_t dev_no;
    int      fd;
    uint8_t  is_linked;
    uint8_t  mac[6];
    void *private;
} bTcpIpNetif_t;

typedef struct
{
    int (*init)(bTcpIpNetif_t *netif);
    void (*loop)(bTcpIpNetif_t *netif);
    void (*reg_callback)(pTcpIpCallback_t cb, void *arg, bTcpIpNetif_t *netif);

    // 网卡相关接口，如果有协议栈管理多张网卡的情况，会传入网卡信息；
    int (*set_mac)(uint8_t mac[6], bTcpIpNetif_t *netif);
    int (*set_ip)(uint32_t ip, uint32_t mask, uint32_t gateway, bTcpIpNetif_t *netif);
    int (*set_link_state)(uint8_t state, bTcpIpNetif_t *netif);
    int (*set_default_netif)(bTcpIpNetif_t *netif);

    // tcp/udp相关接口
    struct
    {
        void *(*new)(bTcpIpNetif_t *pnetif);
        int (*bind)(void *, uint16_t);
        int (*listen)(void *, uint16_t);
        int (*connect)(void *, uint32_t, uint16_t);
        int (*send)(void *, const uint8_t *, uint16_t);
        int (*recv)(void *, uint8_t *, uint16_t);
        int (*delete)(void *);
    } tcp;

    struct
    {
        void *(*new)(bTcpIpNetif_t *pnetif);
        int (*bind)(void *, uint16_t);
        int (*listen)(void *, uint16_t);
        int (*connect)(void *, uint32_t, uint16_t);
        int (*send)(void *, const uint8_t *, uint16_t);
        int (*recv)(void *, uint8_t *, uint16_t);
        int (*delete)(void *);
    } udp;

    uint8_t (*is_readable)(void *);
    uint8_t (*is_writeable)(void *);
} bTcpIpStackIf_t;

///////////////////////////////////////////////////////////
// power meter and analysis, Command  &  Structure
///////////////////////////////////////////////////////////
#define bCMD_HLW811X_REG_CALLBACK 0       // bHlw811xDrvCallback_t
#define bCMD_HLW811X_SOFT_RST 1           // none
#define bCMD_HLW811X_MODE_AC 2            // none
#define bCMD_HLW811X_MODE_DC 3            // none
#define bCMD_HLW811X_SET_RESRATIO_IA 4    // none
#define bCMD_HLW811X_SET_RESRATIO_IB 5    // none
#define bCMD_HLW811X_SET_RESRATIO_U 6     // none
#define bCMD_HLW811X_SET_CALLBACK_ARG 15  // void *

typedef enum
{
    B_EVT_INT_PFA = 0,
    B_EVT_INT_PFB,
} bHlw811xDrvEvent_t;

typedef struct
{
    void (*cb)(bHlw811xDrvEvent_t event, void *arg, void (*release)(void *), void *user_data);
    void *user_data;
} bHlw811xDrvCallback_t;

typedef struct
{
    float RmsU;
    float RmsIA;
    float RmsIB;
    float PowerPA;
    float PowerPB;
    float PowerSA;
    float PowerSB;
    float EnergyA;
    float EnergyB;
    float FreqU;
    float PowerFactorA;
    float PowerFactorB;
    float PhaseAngleA;
    float PhaseAngleB;
} bPowerMeter_hlw811x_t;
///////////////////////////////////////////////////////////
// ads124x, Command  &  Structure
///////////////////////////////////////////////////////////
#define bCMD_ADS124X_REG_CALLBACK 0       //
#define bCMD_ADS124X_START_1CONV 1        // none
#define bCMD_ADS124X_SET_VREF 2           // none
#define bCMD_ADS124X_SET_AIN 3            // none
#define bCMD_ADS124X_SET_AINP 4           // none
#define bCMD_ADS124X_SET_FSC 5            // none
#define bCMD_ADS124X_SET_CURRENT 6        // none
#define bCMD_ADS124X_GET_GDAT 7           // none
#define bCMD_ADS124X_SET_OFC 8            // none
#define bCMD_ADS124X_SET_MUX 9            // none
#define bCMD_ADS124X_SET_BURNOUT 10       // none
#define bCMD_ADS124X_SET_BIAS 11          // none
#define bCMD_ADS124X_SET_PWRDN 12         // none
#define bCMD_ADS124X_SET_PGA 13           // none
#define bCMD_ADS124X_SET_IREF 14          // none
#define bCMD_ADS124X_RESETDUT 15          // none
#define bCMD_ADS124X_SET_DATARATE 16      // none
#define bCMD_ADS124X_SET_GIO 17           // none
#define bCMD_ADS124X_SET_IDAC 18          // none
#define bCMD_ADS124X_SET_CALLBACK_ARG 32  // void *

typedef enum
{
    B_EVT_CONV_STA_OK = 0,

} bAds124xDrvEvent_t;

typedef struct
{
    void (*cb)(bAds124xDrvEvent_t event, void *arg, void (*release)(void *), void *user_data);
    void *user_data;
} bAds124xDrvCallback_t;
///////////////////////////////////////////////////////////
// TM1638 , Command  &  Structure
///////////////////////////////////////////////////////////
#define bCMD_TM1638_SET_DIGIT_TYPE 0  //

typedef enum
{
    B_TM1638_MULTIPLE_DIGIT_HEX = 0,
    B_TM1638_MULTIPLE_DIGIT_CHAR,
    B_TM1638_MULTIPLE_DIGIT_LED,
} bTm1638MultipleDigit_t;

///////////////////////////////////////////////////////////
// BQ769X2 , Command  &  Structure
///////////////////////////////////////////////////////////
typedef struct
{
    uint16_t CellVoltage[16];
    float    Temperature[3];
    uint16_t Stack_Voltage;
    uint16_t Pack_Voltage;
    uint16_t LD_Voltage;
    uint16_t Pack_Current;
    uint16_t AlarmBits;
} bBMS_AFE_BQ769X2_Value_t;

///////////////////////////////////////////////////////////
// ADS125X , Command  &  Structure
///////////////////////////////////////////////////////////
typedef enum
{
  ADS125X_SET_CHANNLE_CMD,
  ADS125X_SET_GAIN_CMD,
  ADS125X_SET_BRATE_CMD,
  ADS125X_READ_CHANNLE_CMD,
  ADS125X_READ_GAIN_CMD,
  ADS125X_READ_BRATE_CMD,
  ADS125X_READ_STATUS_CMD
}bAds125xCommandEnum;

typedef struct 
{
    uint8_t ads125x_channle;
    uint8_t ads125x_gain;
    uint8_t ads125x_brate;
    uint8_t ads125x_status;
    uint32_t ads125x_sum;
    double   ads125x_voltage;
}bAds125xDrvData_t;

///////////////////////////////////////////////////////////
// SMP3011 , Command  &  Structure
///////////////////////////////////////////////////////////
typedef enum
{
  SMP3011_READ_P_CMD,
  SMP3011_READ_T_CMD,
  SMP3011_READ_P_T_CMD,
  SMP3011_READ_OTP_CMD,
  SMP3011_SET_P_UPRANG_CMD,
  SMP3011_SET_P_LOWRANG_CMD,
}bSMP3011CommandEnum;


#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
