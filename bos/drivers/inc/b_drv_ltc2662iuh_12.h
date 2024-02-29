#ifndef __LTC2662IUH_12_H__
#define __LTC2662IUH_12_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ----------------------------------------------*/
#include "b_config.h"
#include "utils/inc/b_util_log.h"
#include "drivers/inc/b_driver.h"

/**
 * \defgroup LTC2662IUH_12_Exported_Defines
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Exported_TypesDefinitions
 * \{
 */
typedef bHalSPIIf_t bLTC2662IUH_12_HalIf_t;

typedef union LT_union_int16_2bytes
{
    int16_t  LT_int16;
    uint16_t LT_uint16;
    uint8_t  LT_byte[2];
} U_LT_Write;

/**
 * \brief LTC2662_CMD_t
Table 1. Command Codes
COMMAND
C3 	C2 	C1 	C0
0 	0 	0 	0 	Write Code to n
1 	0 	0 	0 	Write Code to All
0 	1 	1 	0 	Write Span to n
1 	1 	1 	0 	Write Span to All
0 	0 	0 	1 	Update n (Power Up)
1 	0 	0 	1 	Update All (Power Up)
0 	0 	1 	1 	Write Code to n, Update n (Power Up)
0 	0 	1 	0 	Write Code to n, Update All (Power Up)
1 	0 	1 	0 	Write Code to All, Update All (Power Up)
0 	1 	0 	0 	Power Down n
0 	1 	0 	1 	Power Down Chip
1 	0 	1 	1 	Monitor Mux
1 	1 	0 	0 	Toggle Select
1 	1 	0 	1 	Global Toggle
0 	1 	1 	1 	Config Command
1 	1 	1 	1 	No Operation
*/
typedef enum
{
    LTC_CMD_0 = 0, // write code to n
    LTC_CMD_1,     // write code to all
    LTC_CMD_2,     // write span to n
    LTC_CMD_3,     // write span to all
    LTC_CMD_4,     // update n (power up)
    LTC_CMD_5,     // update all (power up)
    LTC_CMD_6,     // write code to n, update n (power up)
    LTC_CMD_7,     // write code to n, update all (power up)
    LTC_CMD_8,     // write code to all, update all (power up)
    LTC_CMD_9,     // power down n
    LTC_CMD_10,    // power down chip
    LTC_CMD_11,    // monitor mux
    LTC_CMD_12,    // toggle select
    LTC_CMD_13,    // global toggle
    LTC_CMD_14,    // config command
    LTC_CMD_15,    // no operation

    LTC_CMD_MAX
} LTC2662_CMD_t;

/**
 * \brief C3-C0为U8的bit7-bit4
 * COMMAND
C3(bit7) 	C2(bit6) 	C1(bit5) 	C0(bit4)
0 	0 	0 	0 	Write Code to n
1 	0 	0 	0 	Write Code to All
0 	1 	1 	0 	Write Span to n
1 	1 	1 	0 	Write Span to All
0 	0 	0 	1 	Update n (Power Up)
1 	0 	0 	1 	Update All (Power Up)
0 	0 	1 	1 	Write Code to n, Update n (Power Up)
0 	0 	1 	0 	Write Code to n, Update All (Power Up)
1 	0 	1 	0 	Write Code to All, Update All (Power Up)
0 	1 	0 	0 	Power Down n
0 	1 	0 	1 	Power Down Chip
1 	0 	1 	1 	Monitor Mux
1 	1 	0 	0 	Toggle Select
1 	1 	0 	1 	Global Toggle
0 	1 	1 	1 	Config Command
1 	1 	1 	1 	No Operation
*/
typedef enum
{
    WRITE_SPAN_TO_N          = 0x60,
    WRITE_CODE_TO_N_UPDATE_N = 0x30,
    POWER_DOWN_N             = 0x40,
} LTC2662_CMD_DATA_t;

/**
 * \brief LTC2662_DAC_t
表 2. DAC 地址， n
地址
A3 A2 A1 A0
0 0 0 0 DAC 0
0 0 0 1 DAC 1
0 0 1 0 DAC 2
0 0 1 1 DAC 3
0 1 0 0 DAC 4
*/
typedef enum
{
    LTC_DAC_0 = 0,
    LTC_DAC_1,
    LTC_DAC_2,
    LTC_DAC_3,
    LTC_DAC_4,
    LTC_DAC_MAX
} LTC2662_DAC_t;

#define LTC_SPAN_3_125 ((float)3.125)
#define LTC_SPAN_6_25 ((float)6.25)
#define LTC_SPAN_12_5 ((float)12.5)
#define LTC_SPAN_25 ((float)25.0)
#define LTC_SPAN_50 ((float)50.0)
#define LTC_SPAN_100 ((float)100.0)
#define LTC_SPAN_200 ((float)200.0)
#define LTC_SPAN_300 ((float)300.0)

/**
 * \brief LTC2662_SPAN_t
 Table 3. Span Codes
S3 	S2 	S1 	S0	OUTPUT RANGE
External RFSADJ 	FSADJ = VCC
0 	0 	0 	0 	(Hi-Z)
0 	0 	0 	1 	50 • VREF /RFSADJ 	3.125mA
0 	0 	1 	0 	100 • VREF /RFSADJ 	6.25mA
0 	0 	1 	1 	200 • VREF /RFSADJ 	12.5mA
0 	1 	0 	0 	400 • VREF /RFSADJ 	25mA
0 	1 	0 	1 	800 • VREF /RFSADJ 	50mA
0 	1 	1 	0 	1600 • VREF /RFSADJ 	100mA
0 	1 	1 	1 	3200 • VREF /RFSADJ 	200mA
1 	1 	1 	1 	4800 • VREF /RFSADJ 	300mA
1 	0 	0 	0 	(Switch to V–)
 */
typedef enum
{
    LTC_SPAN_3_125mA = 0,
    LTC_SPAN_6_25mA,
    LTC_SPAN_12_5mA,
    LTC_SPAN_25mA,
    LTC_SPAN_50mA,
    LTC_SPAN_100mA,
    LTC_SPAN_200mA,
    LTC_SPAN_300mA,
    LTC_SPAN_MAX
} LTC2662_SPAN_t;

/**
 * \brief
 *  Table 3. Span Codes
S3 	S2 	S1 	S0	OUTPUT RANGE
External RFSADJ 	FSADJ = VCC
0 	0 	0 	0 	(Hi-Z)
0 	0 	0 	1 	50 • VREF /RFSADJ 	3.125mA
0 	0 	1 	0 	100 • VREF /RFSADJ 	6.25mA
0 	0 	1 	1 	200 • VREF /RFSADJ 	12.5mA
0 	1 	0 	0 	400 • VREF /RFSADJ 	25mA
0 	1 	0 	1 	800 • VREF /RFSADJ 	50mA
0 	1 	1 	0 	1600 • VREF /RFSADJ 	100mA
0 	1 	1 	1 	3200 • VREF /RFSADJ 	200mA
1 	1 	1 	1 	4800 • VREF /RFSADJ 	300mA
1 	0 	0 	0 	(Switch to V–)
 */
typedef enum
{
    LTC_SPAN_VALUE_3_125mA = 0X01,
    LTC_SPAN_VALUE_6_25mA  = 0X02,
    LTC_SPAN_VALUE_12_5mA  = 0X03,
    LTC_SPAN_VALUE_25mA    = 0X04,
    LTC_SPAN_VALUE_50mA    = 0X05,
    LTC_SPAN_VALUE_100mA   = 0X06,
    LTC_SPAN_VALUE_200mA   = 0X07,
    LTC_SPAN_VALUE_300mA   = 0X0F,
    LTC_SPAN_VALUE_MAX
} LTC2662_SPAN_VALUE_t;

typedef struct
{
    LTC2662_DAC_t dac_channel;  // 0-4
    uint8_t       status;       // 0:关闭, 1:打开
} bLTC2662_STATUS_t;

typedef struct
{
    LTC2662_DAC_t dac_channel; // 0-4
    float current;       // 电流值,单位mA
} bLTC2662_CONFIG_t;

typedef struct
{
    volatile float
        expect_current;  // 期待的输出电流,即modbus电流强度*0.5mA,bCMD_LTC_SET_CURRENT时候更新

    volatile uint8_t status;      // 输出状态, 0:关闭, 1:打开;bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新

    volatile LTC2662_SPAN_t span; // bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新
    volatile float span_value;    // bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新
    volatile float span_div;      // 对应span曲线下set_value每增加1所代表的值,bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新

    volatile uint16_t set_value;           // 依据modbus的电流强度设置,12bit,0-4095;bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新
    volatile float real_current;           // 实际的输出电流;bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新
    volatile float err_current;            // 输出电流的误差.输出状态为0时,误差为0;输出状态为1时,误差为设置电流与实际电流的差值;bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新
    volatile float err_percentage_current; // 输出电流的误差百分比.输出状态为0时,误差为0;输出状态为1时,值为误差电流绝对值与设置电流绝对值的百分比;bCMD_LTC_EXEC_DAC_X || bCMD_LTC_STOP_DAC_X时更新
} bLTC2662_DAC_ATTRIBUTE_t;

typedef struct
{
    bLTC2662_DAC_ATTRIBUTE_t dac_attribute[5]; // 总共5个,[0-4],实际使用4个
} bLTC2662IUH_12Private_t;
/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Exported_Macros
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Exported_Functions
 * \{
 */

/**
 * }
 */

/**
 * }
 */

/**
 * }
 */

/**
 * }
 */

#ifdef __cplusplus
}
#endif

#endif /* __LTC2662IUH_12_H__ */

/***** Copyright (c) 2024 miniminiminini *****END OF FILE*****/
