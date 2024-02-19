/**
 * \file        b_drv_ltc2662iuh_12.c
 * \version     v0.0.1
 * \date        2024-02-19
 * \author      miniminiminini (405553848@qq.com)
 * \brief       LTC2662IUH_12驱动
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */

/* Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_ltc2662iuh_12.h"

/**
 * \defgroup LTC2662IUH_12_Private_FunctionPrototypes
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Exported_Variables
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Defines
 * \{
 */
#define DRIVER_NAME LTC2662IUH_12

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_TypesDefinitions
 * \{
 */
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
    LTC_CMD_0 = 0,  // write code to n
    LTC_CMD_1,      // write code to all
    LTC_CMD_2,      // write span to n
    LTC_CMD_3,      // write span to all
    LTC_CMD_4,      // update n (power up)
    LTC_CMD_5,      // update all (power up)
    LTC_CMD_6,      // write code to n, update n (power up)
    LTC_CMD_7,      // write code to n, update all (power up)
    LTC_CMD_8,      // write code to all, update all (power up)
    LTC_CMD_9,      // power down n
    LTC_CMD_10,     // power down chip
    LTC_CMD_11,     // monitor mux
    LTC_CMD_12,     // toggle select
    LTC_CMD_13,     // global toggle
    LTC_CMD_14,     // config command
    LTC_CMD_15,     // no operation

    LTC_CMD_MAX
} LTC2662_CMD_t;

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
    LTC_DAC_ALL
} LTC2662_DAC_t;

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
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Macros
 * \{
 */

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bLTC2662IUH_12_HalIf_t, DRIVER_NAME);

static bLTC2662IUH_12Private_t
    bLTC2662IUH_12RunInfo[bDRIVER_HALIF_NUM(bLTC2662IUH_12_HalIf_t, DRIVER_NAME)];

/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Private_Functions
 * \{
 */
static int _bLTC2662IUH_12_FramingHandler(LTC2662_CMD_t cmd, LTC2662_DAC_t dac_x, uint32_t value)
{
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    return 0;
}

/**
 * \brief     写入范围到n
 * \param pdrv
 * \param dac_x
 * \param range
 * \return int
 */
static int _bLTC2662IUH_12_WriteSpanToN(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x,
                                        LTC2662_SPAN_t range)
{
    return 0;
}

/**
 * \brief   写入代码至 n，更新 n（上电）
 * \param pdrv
 * \param dac_x
 * \param range
 * \return int
 */
static int _bLTC2662IUH_12_WriteCodeToNUpdateN(bDriverInterface_t *pdrv, LTC2662_DAC_t dac_x,
                                               LTC2662_SPAN_t range)
{
    return 0;
}

static int _bLTC2662IUH_12Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    return len;
}

static int _bLTC2662IUH_12Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    return 0;
}

static int _bLTC2662IUH_12Open(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    return 0;
}

/**
 * \brief 关断模式
 * \param pdrv
 * \return int
 */
static int _bLTC2662IUH_12Close(bDriverInterface_t *pdrv)
{
    bDRIVER_GET_HALIF(_if, bLTC2662IUH_12_HalIf_t, pdrv);
    return 0;
}
/**
 * }
 */

/**
 * \defgroup LTC2662IUH_12_Exported_Functions
 * \{
 */
int bLTC2662IUH_12_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bLTC2662IUH_12_Init);
    pdrv->read  = _bLTC2662IUH_12Read;
    pdrv->ctl   = _bLTC2662IUH_12Ctl;
    pdrv->close = _bLTC2662IUH_12Close;
    pdrv->_private._p = &bLTC2662IUH_12RunInfo[pdrv->drv_no];
    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_LTC2662IUH_12, bLTC2662IUH_12_Init);
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

/***** Copyright (c) 2024 miniminiminini *****END OF FILE*****/
