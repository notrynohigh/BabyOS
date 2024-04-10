#ifndef __B_CONFIG_H__ 
#define __B_CONFIG_H__ 


#define HW_VERSION 211212
#define FW_VERSION 80106
#define FW_NAME "BabyOS"
#define TICK_FRQ_HZ 1000
#define VENDOR_UBUNTU 1
#define _BOS_ALGO_ENABLE 1
#define _ALGO_MD5_ENABLE 1
#define _ALGO_CRC_ENABLE 1
#define PCF8574_DEFAULT_OUTPUT 0
#define MATRIX_KEYS_ROWS 4
#define MATRIX_KEYS_COLUMNS 4
#define ESP12F_UART_RX_BUF_LEN 1024
#define ESP12F_CMD_BUF_LEN 128
#define ESP12F_CMD_TIMEOUT 5000
#define RS485_RX_BUF_LEN 128
#define RS485_RX_IDLE_MS 50
#define _485_USE_CALLBACK 1
#define _BOS_MODULES_ENABLE 1
#define _MBEDTLS_ENABLE 1
#define MBEDTLS_CONFIG_FILE "b_mbedtls_config.h"
#define _DEBUG_ENABLE 1
#define LOG_LEVEL_ALL 1
#define LOG_BUF_SIZE 256
#define _LOG_VIA_UART 1
#define LOG_UART 0
#define _MEMP_ENABLE 1
#define MEMP_MAX_SIZE 10240
#define _BOS_SERVICES_ENABLE 1


#include "b_type.h" 

#endif 

