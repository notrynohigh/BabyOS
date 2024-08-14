/**
 *!
 * \file        b_drv_hlw811x.h
 * \version     v0.0.1
 * \date        2024/08/14
 * \author      hmchen(chenhaimeng@189.cn)
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
#ifndef __B_DRV_HLW811X_H__
#define __B_DRV_HLW811X_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_driver.h"
/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup HLW811X
 * \{
 */

/**
 * \defgroup HLW811X_Exported_TypesDefinitions
 * \{
 */
//<HALIF 2 SPI_I2C

typedef struct
{
    union
    {
        bHalUartNumber_t _uart;
        bHalSPIIf_t _spi;
    } _if;
    uint8_t    is_spi;
    bHalItIf_t it[2];
} bHLW811X_HalIf_t;


/* Exported Data Types ----------------------------------------------------------*/
/**
 * @brief  Library functions result data type
 */
typedef enum HLW811x_Result_e
{
  HLW811X_OK              = 0,
  HLW811X_FAIL            = 1,
  HLW811X_INVALID_PARAM   = 2,
} HLW811x_Result_t;


/**
 * @brief  Device type
 */
typedef enum HLW811x_Device_e
{
  HLW811X_DEVICE_HLW8110 = 0,
  HLW811X_DEVICE_HLW8112 = 1,
} HLW811x_Device_t;


/**
 * @brief  Channel type
 */
typedef enum HLW811x_CurrentChannel_e
{
  HLW811X_CURRENT_CHANNEL_A = 0,
  HLW811X_CURRENT_CHANNEL_B = 1
} HLW811x_CurrentChannel_t;

/**
 * @brief  PGA gain
 */
typedef enum HLW811x_PGA_e
{
  HLW811X_PGA_1   = 0,
  HLW811X_PGA_2   = 1,
  HLW811X_PGA_4   = 2,
  HLW811X_PGA_8   = 3,
  HLW811X_PGA_16  = 4,
  HLW811X_PGA_NONE = 5
} HLW811x_PGA_t;


/**
 * @brief  Active power calculation method
 */
typedef enum HLW811x_ActivePowCalcMethod_e
{
  HLW811X_ACTIVE_POW_CALC_METHOD_POS_NEG_ALGEBRAIC = 0,
  HLW811X_ACTIVE_POW_CALC_METHOD_POS = 1,
  HLW811X_ACTIVE_POW_CALC_METHOD_POS_NEG_ABSOLUTE = 2
} HLW811x_ActivePowCalcMethod_t;


/**
 * @brief  RMS calculation mode
 * @note   In DC mode, it is mandatory to enable the Waveform
 *         Data.
 */
typedef enum HLW811x_RMSCalcMode_e
{
  HLW811X_RMS_CALC_MODE_NORMAL = 0,
  HLW811X_RMS_CALC_MODE_DC = 1
} HLW811x_RMSCalcMode_t;


/**
 * @brief  Zero crossing detection mode
 */
typedef enum HLW811x_ZeroCrossingMode_e
{
  HLW811X_ZERO_CROSSING_MODE_POSITIVE = 0,
  HLW811X_ZERO_CROSSING_MODE_NEGATIVE = 1,
  HLW811X_ZERO_CROSSING_MODE_BOTH = 2
} HLW811x_ZeroCrossingMode_t;


/**
 * @brief  Interrupt output functionality
 */
typedef enum HLW811x_IntOutFunc_e
{
  HLW811X_INTOUT_FUNC_PULSE_PFA = 0,
  HLW811X_INTOUT_FUNC_PULSE_PFB = 1,
  HLW811X_INTOUT_FUNC_LEAKAGE= 2,
  HLW811X_INTOUT_FUNC_IRQ = 3,
  HLW811X_INTOUT_FUNC_POWER_OVERLOAD = 4,
  HLW811X_INTOUT_FUNC_NEGATIVE_POWER_A = 5,
  HLW811X_INTOUT_FUNC_NEGATIVE_POWER_B = 6,
  HLW811X_INTOUT_FUNC_INSTAN_VALUE_UPDATE_INT = 7,
  HLW811X_INTOUT_FUNC_AVG_UPDATE_INT = 8,
  HLW811X_INTOUT_FUNC_VOLTAGE_ZERO_CROSSING = 9,
  HLW811X_INTOUT_FUNC_CURRENT_ZERO_CROSSING_A = 10,
  HLW811X_INTOUT_FUNC_CURRENT_ZERO_CROSSING_B = 11,
  HLW811X_INTOUT_FUNC_OVERVOLTAGE = 12,
  HLW811X_INTOUT_FUNC_UNDERVOLTAGE = 13,
  HLW811X_INTOUT_FUNC_OVERCURRENT_A = 14,
  HLW811X_INTOUT_FUNC_OVERCURRENT_B = 15,
  HLW811X_INTOUT_FUNC_NO_CHANGE = 16
} HLW811x_IntOutFunc_t;


/**
 * @brief  Data update frequency
 */
typedef enum HLW811x_DataUpdateFreq_e
{
  HLW811X_DATA_UPDATE_FREQ_3_4HZ = 0,
  HLW811X_DATA_UPDATE_FREQ_6_8HZ = 1,
  HLW811X_DATA_UPDATE_FREQ_13_65HZ = 2,
  HLW811X_DATA_UPDATE_FREQ_27_3HZ = 3
} HLW811x_DataUpdateFreq_t;


/**
 * @brief  Enable/Disable type
 */
typedef enum HLW811x_EnDis_e
{
  HLW811X_ENDIS_NOCHANGE = -1,
  HLW811X_ENDIS_DISABLE = 0,
  HLW811X_ENDIS_ENABLE = 1
} HLW811x_EnDis_t;


/**
 * @brief  Communication type
 */
typedef enum HLW811x_Communication_e
{
  HLW811X_COMMUNICATION_SPI  = 0,
  HLW811X_COMMUNICATION_UART = 1,
} HLW811x_Communication_t;


typedef struct
{
	HLW811x_Device_t Device;
	// Coefficients	
	struct
	{
		uint16_t RmsIAC;
		uint16_t RmsIBC;
		uint16_t RmsUC;
		uint16_t PowerPAC;
		uint16_t PowerPBC;
		uint16_t PowerSC;
		uint16_t EnergyAC;
		uint16_t EnergyBC;
	} CoefReg;
	 struct
  {
    float KU;
    float KIA;
    float KIB;
  } ResCoef;
	struct
	{
		HLW811x_PGA_t U;
		HLW811x_PGA_t IA;
		HLW811x_PGA_t IB;
	} PGA;

  uint16_t HFconst;
  uint32_t CLKI;
  HLW811x_CurrentChannel_t CurrentChannel;
	
} bHlw811xPrivate_t;

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
