/**
 *!
 * \file        b_drv_apds9930.c
 * \version     v0.0.1
 * \date        2021/04/29
 * \author      polyGithub(baoli.chen@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2021 polyGithub
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
#include "drivers/inc/b_drv_apds9930.h"

#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup APDS9930
 * \{
 */

/**
 * \defgroup APDS9930_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup APDS9930_Private_Defines
 * \{
 */
#define DRIVER_NAME APDS9930


#define APDS9930_ENABLE		0x00
#define APDS9930_ATIME		0x01
#define APDS9930_PTIME		0x02
#define APDS9930_WTIME		0x03
#define APDS9930_AILTL		0x04
#define APDS9930_AILTH		0x05
#define APDS9930_AIHTH		0x06
#define APDS9930_AIHTL		0x07
#define APDS9930_PILTL		0x08
#define APDS9930_PILTH		0x09
#define APDS9930_PIHTL		0x0A
#define APDS9930_PIHTH		0x0B
#define APDS9930_PERS		0x0C
#define APDS9930_CONFIG		0x0D
#define APDS9930_PPULSE		0x0E
#define APDS9930_CONTROL	0x0F
#define APDS9930_ID				0x12
#define APDS9930_STATUS		0x13
#define APDS9930_Ch0DATAL	0x14
#define APDS9930_Ch0DATAH	0x15
#define APDS9930_Ch1DATAL	0x16
#define APDS9930_Ch1DATAH	0x17
#define APDS9930_PDATAL		0x18
#define APDS9930_PDATAH		0x19
#define APDS9930_POFFSET	0x1E

#define APDS9930_ENABLE_SAI		1<<6
#define APDS9930_ENABLE_PIEN		1<<5
#define APDS9930_ENABLE_AIEN		1<<4
#define APDS9930_ENABLE_WEN		1<<3
#define APDS9930_ENABLE_PEN		1<<2
#define APDS9930_ENABLE_AEN		1<<1
#define APDS9930_ENABLE_PON		1<<0

#define APDS9930_ATIME_CYCLES_1			0xFF
#define APDS9930_ATIME_CYCLES_10		0xF6
#define APDS9930_ATIME_CYCLES_37		0xDB
#define APDS9930_ATIME_CYCLES_64		0xC0
#define APDS9930_ATIME_CYCLES_256		0x00

#define APDS9930_PTIME_CYCLE_1		0xFF

#define APDS9930_WTime_1ms			0xFF
#define APDS9930_WTime_74ms			0xB6
#define APDS9930_WTime_256ms		0x00

#define APDS9930_P_Cycle_AlL		0x00
#define APDS9930_P_Cycle_1			0x10
#define APDS9930_P_Cycle_2			0x20
#define APDS9930_P_Cycle_3			0x30
#define APDS9930_P_Cycle_4			0x40
#define APDS9930_P_Cycle_5			0x50
#define APDS9930_P_Cycle_6			0x60
#define APDS9930_P_Cycle_7			0x70
#define APDS9930_P_Cycle_8			0x80
#define APDS9930_P_Cycle_9			0x90
#define APDS9930_P_Cycle_10			0xA0
#define APDS9930_P_Cycle_11			0xB0
#define APDS9930_P_Cycle_12			0xC0
#define APDS9930_P_Cycle_13			0xD0
#define APDS9930_P_Cycle_14			0xE0
#define APDS9930_P_Cycle_15			0xF0

#define APDS9930_ALS_Cycle_All		0x00
#define APDS9930_ALS_Cycle_1		0x01
#define APDS9930_ALS_Cycle_2		0x02
#define APDS9930_ALS_Cycle_3		0x03
#define APDS9930_ALS_Cycle_4		0x04
#define APDS9930_ALS_Cycle_5		0x05
#define APDS9930_ALS_Cycle_6		0x06
#define APDS9930_ALS_Cycle_7		0x07
#define APDS9930_ALS_Cycle_8		0x08
#define APDS9930_ALS_Cycle_9		0x09
#define APDS9930_ALS_Cycle_10		0x0A
#define APDS9930_ALS_Cycle_11		0x0B
#define APDS9930_ALS_Cycle_12		0x0C
#define APDS9930_ALS_Cycle_13		0x0D
#define APDS9930_ALS_Cycle_14		0x0E
#define APDS9930_ALS_Cycle_15		0x0F

#define APDS9930_CONFG_AGL			0x00
#define APDS9930_CONFG_WLONG		0x02
#define APDS9930_CONFG_PDL			0x04

#define APDS9930_CNTRL_LED_Drive_STR_100mA			0x00
#define APDS9930_CNTRL_LED_Drive_STR_50mA			0x40
#define APDS9930_CNTRL_LED_Drive_STR_25mA			0x80
#define APDS9930_CNTRL_LED_Drive_STR_12_5mA			0xC0

#define APDS9930_CNTRL_PDIODE_CH1			0x20

#define APDS9930_CNTRL_PGAIN_1X			0x00
#define APDS9930_CNTRL_PGAIN_2X			0x04
#define APDS9930_CNTRL_PGAIN_4X			0x08
#define APDS9930_CNTRL_PGAIN_8X			0x0C

#define APDS9930_CNTRL_AGAIN_1X			0x00
#define APDS9930_CNTRL_AGAIN_2X			0x01
#define APDS9930_CNTRL_AGAIN_16X			0x02
#define APDS9930_CNTRL_AGAIN_120X			0x03

#define APDS9930_STATUS_REG_PSAT			1 << 6
#define APDS9930_STATUS_REG_PINT			1 << 5
#define APDS9930_STATUS_REG_AINT			1 << 4
#define APDS9930_STATUS_REG_PVALID			1 << 1
#define APDS9930_STATUS_REG_AVALID			1 << 0

#define APDS9930_DEVICE_ID				0x39
/**
 * \}
 */

/**
 * \defgroup APDS9930_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup APDS9930_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bAPDS9930_HalIf_t, DRIVER_NAME);

/**
 * \}
 */

/**
 * \defgroup APDS9930_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup APDS9930_Private_Functions
 * \{
 */
static int _bAPDS9930ReadRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data, uint16_t len)
{
	bDRIVER_GET_HALIF(_if, bAPDS9930_HalIf_t, pdrv);
	reg = reg | 0xA0;
	bHalI2CMemRead(_if, reg, 1, data, len);
	return 0;
}

static int _bAPDS9930WriteRegs(bDriverInterface_t *pdrv, uint8_t reg, uint8_t *data, uint16_t len)
{
	bDRIVER_GET_HALIF(_if, bAPDS9930_HalIf_t, pdrv);
	reg = reg | 0x80;
	bHalI2CMemWrite(_if, reg, 1, data, len);

	return 0;
}
static uint8_t _bAPDS9930GetID(bDriverInterface_t *pdrv)
{
    uint8_t id = 0;
    _bAPDS9930ReadRegs(pdrv, APDS9930_ID, &id, 1);
    b_log("apds9930 id:%x\n", id);
    return id;
}

static void _bAPDS9930DefaultCfg(bDriverInterface_t *pdrv)
{
	uint8_t reg = 0;
	
	reg = 0x00;
	_bAPDS9930WriteRegs(pdrv, APDS9930_ENABLE, &reg, 1);
	
	reg = APDS9930_ATIME_CYCLES_1;
	_bAPDS9930WriteRegs(pdrv, APDS9930_ATIME, &reg, 1);
	
	reg = APDS9930_PTIME_CYCLE_1;
	_bAPDS9930WriteRegs(pdrv, APDS9930_PTIME, &reg, 1);
	
	reg = APDS9930_WTime_1ms;
	_bAPDS9930WriteRegs(pdrv, APDS9930_WTIME, &reg, 1);

	reg = 1;
	_bAPDS9930WriteRegs(pdrv, APDS9930_PPULSE, &reg, 1);

	reg = APDS9930_CNTRL_LED_Drive_STR_100mA | APDS9930_CNTRL_PDIODE_CH1 | APDS9930_CNTRL_AGAIN_1X | APDS9930_CNTRL_PGAIN_1X;;
	_bAPDS9930WriteRegs(pdrv, APDS9930_CONTROL, &reg, 1);

	reg = APDS9930_ENABLE_PON | APDS9930_ENABLE_AEN | APDS9930_ENABLE_PEN | APDS9930_ENABLE_WEN;;
	_bAPDS9930WriteRegs(pdrv, APDS9930_ENABLE, &reg, 1);
	
	bHalDelayMs(20);

}
static int _bAPDS9930Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    
    bProximity_AmbientLightsensor_t *ptmp = (bProximity_AmbientLightsensor_t *)pbuf;
    int              i    = 0;
    uint8_t          c    = len / sizeof(bProximity_AmbientLightsensor_t);

    for (i = 0; i < c; i++)
    {
			_bAPDS9930ReadRegs(pdrv, APDS9930_Ch0DATAL, (uint8_t *)&ptmp[i].ALS_Channel_0, 2);
			_bAPDS9930ReadRegs(pdrv, APDS9930_Ch1DATAL, (uint8_t *)&ptmp[i].ALS_Channel_1, 2);
			_bAPDS9930ReadRegs(pdrv, APDS9930_PDATAL, 	(uint8_t *)&ptmp[i].Prox_data, 2);
    }
    return (c * sizeof(bProximity_AmbientLightsensor_t));
}

/**
 * \}
 */

/**
 * \addtogroup APDS9930_Exported_Functions
 * \{
 */
int bAPDS9930_Init(bDriverInterface_t *pdrv)
{
		uint8_t id = 0;
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bAPDS9930_Init);
    pdrv->read       = _bAPDS9930Read;
    pdrv->_private.v = 0;
		id = _bAPDS9930GetID(pdrv);
		if ( (id != APDS9930_DEVICE_ID) && (id != 0x30) )
    {
        return -1;
    }
    _bAPDS9930DefaultCfg(pdrv);

    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_APDS9930, bAPDS9930_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section 
#endif
/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/************************ Copyright (c) 2021 polyGithub *****END OF FILE****/
