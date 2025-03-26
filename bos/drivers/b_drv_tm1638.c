/**
 *!
 * \file        b_drv_tm1638.c
 * \version     v0.0.1
 * \date        2024/09/18
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

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_tm1638.h"
#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup TM1638
 * \{
 */

/**
 * \defgroup TM1638_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TM1638_Private_Defines
 * \{
 */

#define DRIVER_NAME TM1638

/** Device Identification (Who am I) **/

/**
 * \}
 */

/**
 * \defgroup TM1638_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TM1638_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bTM1638_HalIf_t, DRIVER_NAME);

static bTm1638Private_t bTm1638RunInfo[bDRIVER_HALIF_NUM(bTM1638_HalIf_t, DRIVER_NAME)];


/* Private Constants ------------------------------------------------------------*/
/**
 * @brief  Commands
 */
TM1638_Result_t
TM1638_SetMultipleDigit(bDriverInterface_t *pdrv, const uint8_t *DigitData,
                        uint8_t StartAddr, uint8_t Count);

/* Private Constants ------------------------------------------------------------*/
/**
 * @brief  Instruction description
 */
#define DataInstructionSet            0x40  // 0b01000000
#define DisplayControlInstructionSet  0x80  // 0b10000000
#define AddressInstructionSet         0xC0  // 0b11000000

/**
 * @brief  Data instruction set
 */
#define WriteDataToRegister   0x00  // 0b00000000
#define ReadKeyScanData       0x02  // 0b00000010
#define AutoAddressAdd        0x00  // 0b00000000
#define FixedAddress          0x04  // 0b00000100
#define NormalMode            0x00  // 0b00000000
#define TestMode              0x08  // 0b00001000

/**
 * @brief  Display ControlInstruction Set
 */
#define ShowTurnOff   0x00  // 0b00000000
#define ShowTurnOn    0x08  // 0b00001000


/* Private variables ------------------------------------------------------------*/
/**
 * @brief  Convert HEX number to Seven-Segment code
 */
const uint8_t HexTo7Seg[40] =
{
  0x3F, // 0
  0x06, // 1
  0x5B, // 2
  0x4F, // 3
  0x66, // 4
  0x6D, // 5
  0x7D, // 6
  0x07, // 7
  0x7F, // 8
  0x6F, // 9
  0x77, // A
  0x7c, // b
  0x39, // C
  0x5E, // d
  0x79, // E
  0x71, // F
  0x6F, // g
  0x3D, // G
  0x74, // h
  0x76, // H
  0x05, // i
  0x06, // I
  0x0D, // j
  0x30, // l
  0x38, // L
  0x54, // n
  0x37, // N
  0x5C, // o
  0x3F, // O
  0x73, // P
  0x67, // q
  0x50, // r
  0x6D, // S
  0x78, // t
  0x1C, // u
  0x3E, // U
  0x66, // y
  0x08, // _
  0x40, // -
  0x01  // Overscore
};



/**
 ==================================================================================
                            ##### Public Functions #####                           
 ==================================================================================
 */

static inline void
TM1638_StartComunication(bDriverInterface_t *pdrv)
{

  	bDRIVER_GET_HALIF(_if, bTM1638_HalIf_t, pdrv);
	bHalGpioWritePin(_if->stb.port, _if->stb.pin, 0);
}

static inline void
TM1638_StopComunication(bDriverInterface_t *pdrv)
{

	bDRIVER_GET_HALIF(_if, bTM1638_HalIf_t, pdrv);
	bHalGpioWritePin(_if->stb.port, _if->stb.pin, 1);
}

static void
TM1638_WriteBytes(bDriverInterface_t *pdrv,
                  const uint8_t *Data, uint8_t NumOfBytes)
{
	bDRIVER_GET_HALIF(_if, bTM1638_HalIf_t, pdrv);
	uint8_t i, j, Buff;

//	bHalGpioConfig(_if->dio.port, _if->dio.pin, B_HAL_GPIO_OUTPUT, B_HAL_GPIO_NOPULL);
	for (j = 0; j < NumOfBytes; j++)
	{
		for (i = 0, Buff = Data[j]; i < 8; ++i, Buff >>= 1)
		{
			bHalGpioWritePin(_if->clk.port, _if->clk.pin, 0);
			bHalDelayUs(1);
			bHalGpioWritePin(_if->dio.port, _if->dio.pin, Buff & 0x01);
			bHalDelayUs(1);
			bHalGpioWritePin(_if->clk.port, _if->clk.pin, 1);
			bHalDelayUs(1);
		}
	}
}

static void
TM1638_ReadBytes(bDriverInterface_t *pdrv,
                 uint8_t *Data, uint8_t NumOfBytes)
{
	bDRIVER_GET_HALIF(_if, bTM1638_HalIf_t, pdrv);
	uint8_t i, j, Buff;

//	bHalGpioConfig(_if->dio.port, _if->dio.pin, B_HAL_GPIO_INPUT, B_HAL_GPIO_NOPULL);

	bHalDelayUs(5);

	for (j = 0; j < NumOfBytes; j++)
	{
		for (i = 0, Buff = 0; i < 8; i++)
		{
			bHalGpioWritePin(_if->clk.port, _if->clk.pin, 0);
			bHalDelayUs(1);
			bHalGpioWritePin(_if->clk.port, _if->clk.pin, 1);
			Buff |= (bHalGpioReadPin(_if->dio.port, _if->dio.pin) << i);
			bHalDelayUs(1);
		}

		Data[j] = Buff;
		bHalDelayUs(2);
	}
}

static void
TM1638_SetMultipleDisplayRegister(bDriverInterface_t *pdrv,
                                  const uint8_t *DigitData,
                                  uint8_t StartAddr, uint8_t Count)
{
  uint8_t Data = DataInstructionSet | WriteDataToRegister |
                 AutoAddressAdd | NormalMode;

  TM1638_StartComunication(pdrv);
  TM1638_WriteBytes(pdrv, &Data, 1);
  TM1638_StopComunication(pdrv);

  Data = AddressInstructionSet | StartAddr;

  TM1638_StartComunication(pdrv);
  TM1638_WriteBytes(pdrv, &Data, 1);
  TM1638_WriteBytes(pdrv, DigitData, Count);
  TM1638_StopComunication(pdrv);
}

static void
TM1638_ScanKeyRegs(bDriverInterface_t *pdrv, uint8_t *KeyRegs)
{
  uint8_t Data = DataInstructionSet | ReadKeyScanData |
                 AutoAddressAdd | NormalMode;

  TM1638_StartComunication(pdrv);
  TM1638_WriteBytes(pdrv, &Data, 1);
  TM1638_ReadBytes(pdrv, KeyRegs, 4);
  TM1638_StopComunication(pdrv);
}



/**
 ==================================================================================
                           ##### Common Functions #####                            
 ==================================================================================
 */

/**
 * @brief  Initialize TM1638.
 * @param  pdrv: Pointer to pdrv
 * @param  Type: Determine the type of display
 *         - TM1638DisplayTypeComCathode: Common-Cathode
 *         - TM1638DisplayTypeComAnode:   Common-Anode
 * @note   If 'TM1638_CONFIG_SUPPORT_COM_ANODE' switch is set to 0, the 'Type'
 *         argument will be ignored 
 *         
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful.
 */
TM1638_Result_t
TM1638_Init(bDriverInterface_t *pdrv, uint8_t Type)
{
	bDRIVER_GET_PRIVATE(_priv, bTm1638Private_t, pdrv);
	_priv->DisplayType = TM1638DisplayTypeComCathode;

#if TM1638_CONFIG_SUPPORT_COM_ANODE
  for (uint8_t i = 0; i < 16; i++)
  {
    _priv->DisplayRegister[i] = 0;
  }
  if (Type == TM1638DisplayTypeComCathode)
    _priv->DisplayType = TM1638DisplayTypeComCathode;
  else
    _priv->DisplayType = TM1638DisplayTypeComAnode;
#endif

	_priv->Tm1638MultipleDigit = B_TM1638_MULTIPLE_DIGIT_HEX;
  
  return TM1638_OK;
}

/**
 * @brief  De-Initialize TM1638.
 * @param  Handler: Pointer to handler
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful.
 */
TM1638_Result_t
TM1638_DeInit(bDriverInterface_t *pdrv)
{
//  Handler->PlatformDeInit();
  return TM1638_OK;
}



/**
 ==================================================================================
                        ##### Public Display Functions #####                       
 ==================================================================================
 */

/**
 * @brief  Config display parameters
 * @param  Handler: Pointer to handler
 * @param  Brightness: Set brightness level
 *         - 0: Display pulse width is set as 1/16
 *         - 1: Display pulse width is set as 2/16
 *         - 2: Display pulse width is set as 4/16
 *         - 3: Display pulse width is set as 10/16
 *         - 4: Display pulse width is set as 11/16
 *         - 5: Display pulse width is set as 12/16
 *         - 6: Display pulse width is set as 13/16
 *         - 7: Display pulse width is set as 14/16
 * 
 * @param  DisplayState: Set display ON or OFF
 *         - TM1638DisplayStateOFF: Set display state OFF
 *         - TM1638DisplayStateON: Set display state ON
 * 
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful
 */
TM1638_Result_t
TM1638_ConfigDisplay(bDriverInterface_t *pdrv,
                     uint8_t Brightness, uint8_t DisplayState)
{
  uint8_t Data = DisplayControlInstructionSet;
  Data |= Brightness & 0x07;
  Data |= (DisplayState) ? (ShowTurnOn) : (ShowTurnOff);

  TM1638_StartComunication(pdrv);
  TM1638_WriteBytes(pdrv, &Data, 1);
  TM1638_StopComunication(pdrv);

  return TM1638_OK;
}


/**
 * @brief  Set data to single digit in 7-segment format
 * @param  pdrv: Pointer to pdrv
 * @param  DigitData: Digit data
 * @param  DigitPos: Digit position
 *         - 0: Seg1
 *         - 1: Seg2
 *         - .
 *         - .
 *         - .
 * 
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful
 */
TM1638_Result_t
TM1638_SetSingleDigit(bDriverInterface_t *pdrv,
                      uint8_t DigitData, uint8_t DigitPos)
{ 
	bDRIVER_GET_PRIVATE(_priv, bTm1638Private_t, pdrv);
	if (_priv->DisplayType == TM1638DisplayTypeComCathode)
		TM1638_SetMultipleDisplayRegister(pdrv, &DigitData, DigitPos, 1);
#if (TM1638_CONFIG_SUPPORT_COM_ANODE)
	else
		TM1638_SetMultipleDigit(pdrv, &DigitData, DigitPos, 1);
#endif
	return TM1638_OK;
}


/**
 * @brief  Set data to multiple digits in 7-segment format
 * @param  Handler: Pointer to handler
 * @param  DigitData: Array to Digits data
 * @param  StartAddr: First digit position
 *         - 0: Seg1
 *         - 1: Seg2
 *         - .
 *         - .
 *         - .
 * 
 * @param  Count: Number of segments to write data
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful
 */
TM1638_Result_t
TM1638_SetMultipleDigit(bDriverInterface_t *pdrv, const uint8_t *DigitData,
                        uint8_t StartAddr, uint8_t Count)
{
  uint8_t Shift = 0;
  uint8_t DigitDataBuff = 0;
  uint8_t i = 0, j = 0;

	bDRIVER_GET_PRIVATE(_priv, bTm1638Private_t, pdrv);
	if (_priv->DisplayType == TM1638DisplayTypeComCathode)
		TM1638_SetMultipleDisplayRegister(pdrv, DigitData, StartAddr, Count);
#if (TM1638_CONFIG_SUPPORT_COM_ANODE)
	else
	{
		for (j = 0; j < Count; j++)
		{
			  DigitDataBuff = DigitData[j];

			  if ((j + StartAddr) >= 0 && (j + StartAddr) <= 7)
			  {
				Shift = j + StartAddr;
				i = 0;
			  }
			  else if ((j + StartAddr) == 8 || (j + StartAddr) == 9)
			  {
				Shift = (j + StartAddr) - 8;
				i = 1;
			  }
			  else
			  {
				i = 16;
			  }

			  for (; i < 16; i += 2, DigitDataBuff >>= 1)
			  {
				if (DigitDataBuff & 0x01)
				  _priv->DisplayRegister[i] |= (1 << Shift);
				else
				  _priv->DisplayRegister[i] &= ~(1 << Shift);
			  }
		}
		TM1638_SetMultipleDisplayRegister(pdrv, _priv->DisplayRegister, 0, 16);
	}
#endif

  return TM1638_OK;
}

/**
 * @brief  Set data to multiple digits in 7-segment format
 * @param  Handler: Pointer to handler
 * @param  DigitData: Digit data (0, 1, ... , 15, a, A, b, B, ... , f, F) 
 * @param  DigitPos: Digit position
 *         - 0: Seg1
 *         - 1: Seg2
 *         - .
 *         - .
 *         - .
 * 
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful
 */
TM1638_Result_t
TM1638_SetSingleDigit_HEX(bDriverInterface_t *pdrv,
                          uint8_t DigitData, uint8_t DigitPos)
{
  uint8_t DigitDataHEX = 0;
  uint8_t DecimalPoint = DigitData & 0x80;

  DigitData &= 0x7F;

  if (DigitData <= 15)
  {
    DigitDataHEX = HexTo7Seg[DigitData] | DecimalPoint;
  }
  else
  {
    switch (DigitData)
    {
    case 'A':
    case 'a':
      DigitDataHEX = HexTo7Seg[0x0A] | DecimalPoint;
      break;

    case 'B':
    case 'b':
      DigitDataHEX = HexTo7Seg[0x0B] | DecimalPoint;
      break;

    case 'C':
    case 'c':
      DigitDataHEX = HexTo7Seg[0x0C] | DecimalPoint;
      break;

    case 'D':
    case 'd':
      DigitDataHEX = HexTo7Seg[0x0D] | DecimalPoint;
      break;

    case 'E':
    case 'e':
      DigitDataHEX = HexTo7Seg[0x0E] | DecimalPoint;
      break;

    case 'F':
    case 'f':
      DigitDataHEX = HexTo7Seg[0x0F] | DecimalPoint;
      break;

    default:
      DigitDataHEX = 0;
      break;
    }
  }

  return TM1638_SetSingleDigit(pdrv, DigitDataHEX, DigitPos);
}


/**
 * @brief  Set data to multiple digits in hexadecimal format
 * @param  Handler: Pointer to handler
 * @param  DigitData: Array to Digits data. 
 *                    (0, 1, ... , 15, a, A, b, B, ... , f, F)
 * 
 * @param  StartAddr: First digit position
 *         - 0: Seg1
 *         - 1: Seg2
 *         - .
 *         - .
 *         - .
 * 
 * @param  Count: Number of segments to write data
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful
 */
TM1638_Result_t
TM1638_SetMultipleDigit_HEX(bDriverInterface_t *pdrv, const uint8_t *DigitData,
                            uint8_t StartAddr, uint8_t Count)
{
  uint8_t DigitDataHEX[16];
  uint8_t DecimalPoint = 0;

  for (uint8_t i = 0; i < Count; i++)
  {
    DecimalPoint = DigitData[i] & 0x80;

    if ((DigitData[i] & 0x7F) >= 0 && (DigitData[i] & 0x7F) <= 15)
    {
      DigitDataHEX[i] = HexTo7Seg[DigitData[i] & 0x7F] | DecimalPoint;
    }
    else
    {
      switch (DigitData[i] & 0x7F)
      {
      case 'A':
      case 'a':
        DigitDataHEX[i] = HexTo7Seg[0x0A] | DecimalPoint;
        break;

      case 'B':
      case 'b':
        DigitDataHEX[i] = HexTo7Seg[0x0B] | DecimalPoint;
        break;

      case 'C':
      case 'c':
        DigitDataHEX[i] = HexTo7Seg[0x0C] | DecimalPoint;
        break;

      case 'D':
      case 'd':
        DigitDataHEX[i] = HexTo7Seg[0x0D] | DecimalPoint;
        break;

      case 'E':
      case 'e':
        DigitDataHEX[i] = HexTo7Seg[0x0E] | DecimalPoint;
        break;

      case 'F':
      case 'f':
        DigitDataHEX[i] = HexTo7Seg[0x0F] | DecimalPoint;
        break;

      default:
        DigitDataHEX[i] = 0;
        break;
      }
    }
	TM1638_SetSingleDigit(pdrv,
                          DigitDataHEX[i], StartAddr+i*2);
  }

//  return TM1638_SetMultipleDigit(pdrv,
//                                 (const uint8_t *)DigitDataHEX, StartAddr, Count);
    return TM1638_OK;
}


/**
 * @brief  Set data to multiple digits in char format
 * @param  Handler: Pointer to handler
 * @param  DigitData: Array to Digits data. 
 *                    Supported chars 0,1,2,3,4,5,6,7,8,9
 *                                    A,b,C,d,E,F,g,G,h,H,i,I,j,l,L,n,N,o,O,P,q,r,S,
 *                                    t,u,U,y,_,-,Overscore (use ~ to set)
 * 
 * @param  StartAddr: First digit position
 *         - 0: Seg1
 *         - 1: Seg2
 *         - .
 *         - .
 *         - .
 * 
 * @param  Count: Number of segments to write data
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful
 */
TM1638_Result_t
TM1638_SetMultipleDigit_CHAR(bDriverInterface_t *pdrv, const uint8_t *DigitData,
                            uint8_t StartAddr, uint8_t Count)
{
  uint8_t DigitDataHEX[10];
  uint8_t DecimalPoint = 0;

  for (uint8_t i = 0; i < Count; i++)
  {
    DecimalPoint = DigitData[i] & 0x80;

    // numbers 0 - 9
    if ((DigitData[i] & 0x7F) >= (uint8_t)'0' && (DigitData[i] & 0x7F) <= (uint8_t)'9')
    {
      DigitDataHEX[i] = HexTo7Seg[(DigitData[i]-48) & 0x7F] | DecimalPoint;
    }
    else
    {
      switch (DigitData[i] & 0x7F)
      {
      case 'A':
      case 'a':
        DigitDataHEX[i] = HexTo7Seg[0x0A] | DecimalPoint;
        break;

      case 'B':
      case 'b':
        DigitDataHEX[i] = HexTo7Seg[0x0B] | DecimalPoint;
        break;

      case 'C':
      case 'c':
        DigitDataHEX[i] = HexTo7Seg[0x0C] | DecimalPoint;
        break;

      case 'D':
      case 'd':
        DigitDataHEX[i] = HexTo7Seg[0x0D] | DecimalPoint;
        break;

      case 'E':
      case 'e':
        DigitDataHEX[i] = HexTo7Seg[0x0E] | DecimalPoint;
        break;

      case 'F':
      case 'f':
        DigitDataHEX[i] = HexTo7Seg[0x0F] | DecimalPoint;
        break;

      case 'g':
        DigitDataHEX[i] = HexTo7Seg[0x10] | DecimalPoint;
      break;
      
      case 'G':
        DigitDataHEX[i] = HexTo7Seg[0x11] | DecimalPoint;
      break;

      case 'h':
        DigitDataHEX[i] = HexTo7Seg[0x12] | DecimalPoint;
      break;
      
      case 'H':
        DigitDataHEX[i] = HexTo7Seg[0x13] | DecimalPoint;
      break;

      case 'i':
        DigitDataHEX[i] = HexTo7Seg[0x14] | DecimalPoint;
      break;
      
      case 'I':
        DigitDataHEX[i] = HexTo7Seg[0x15] | DecimalPoint;
      break;

      case 'j':
      case 'J':
        DigitDataHEX[i] = HexTo7Seg[0x16] | DecimalPoint;
      break;

      case 'l':
        DigitDataHEX[i] = HexTo7Seg[0x17] | DecimalPoint;
      break;

      case 'L':
        DigitDataHEX[i] = HexTo7Seg[0x18] | DecimalPoint;
      break;

      case 'n':
        DigitDataHEX[i] = HexTo7Seg[0x19] | DecimalPoint;
      break;
      
      case 'N':
        DigitDataHEX[i] = HexTo7Seg[0x1A] | DecimalPoint;
      break;

      case 'o':
        DigitDataHEX[i] = HexTo7Seg[0x1B] | DecimalPoint;
      break;
      
      case 'O':
        DigitDataHEX[i] = HexTo7Seg[0x1C] | DecimalPoint;
      break;

      case 'p':
      case 'P':
        DigitDataHEX[i] = HexTo7Seg[0x1D] | DecimalPoint;
      break;

      case 'q':
      case 'Q':
        DigitDataHEX[i] = HexTo7Seg[0x1E] | DecimalPoint;
      break;

      case 'r':
      case 'R':
        DigitDataHEX[i] = HexTo7Seg[0x1F] | DecimalPoint;
      break;

      case 's':
      case 'S':
        DigitDataHEX[i] = HexTo7Seg[0x20] | DecimalPoint;
      break;

      case 't':
      case 'T':
        DigitDataHEX[i] = HexTo7Seg[0x21] | DecimalPoint;
      break;

      case 'u':
        DigitDataHEX[i] = HexTo7Seg[0x22] | DecimalPoint;
      break;

      case 'U':
        DigitDataHEX[i] = HexTo7Seg[0x23] | DecimalPoint;
      break;

      case 'y':
      case 'Y':
        DigitDataHEX[i] = HexTo7Seg[0x24] | DecimalPoint;
      break;

      case '_':
        DigitDataHEX[i] = HexTo7Seg[0x25] | DecimalPoint;
      break;

      case '-':
        DigitDataHEX[i] = HexTo7Seg[0x26] | DecimalPoint;
      break;

      case '~':
        DigitDataHEX[i] = HexTo7Seg[0x27] | DecimalPoint;
      break;

      default:
        DigitDataHEX[i] = 0;
        break;
      }
    }
	TM1638_SetSingleDigit(pdrv,
					  DigitDataHEX[i], StartAddr+i*2);
  }

//  return TM1638_SetMultipleDigit(pdrv,
//                                 (const uint8_t *)DigitDataHEX, StartAddr, Count);
      return TM1638_OK;
}



/** 
 ==================================================================================
                      ##### Public Keypad Functions #####                         
 ==================================================================================
 */

/**
 * @brief  Scan all 24 keys connected to TM1638
 * @note   
 *                   SEG1         SEG2         SEG3       ......      SEG8
 *                     |            |            |                      |
 *         K1  --  |K1_SEG1|    |K1_SEG2|    |K1_SEG3|    ......    |K1_SEG8|
 *         K2  --  |K2_SEG1|    |K2_SEG2|    |K2_SEG3|    ......    |K2_SEG8|
 *         K3  --  |K3_SEG1|    |K3_SEG2|    |K3_SEG3|    ......    |K3_SEG8|
 * 
 * @param  Handler: Pointer to handler
 * @param  Keys: pointer to save key scan result
 *         - bit0=>K1_SEG1, bit1=>K1_SEG2, ..., bit7=>K1_SEG8,
 *         - bit8=>K2_SEG1, bit9=>K2_SEG2, ..., bit15=>K2_SEG8,
 *         - bit16=>K3_SEG1, bit17=>K3_SEG2, ..., bit23=>K3_SEG8,
 * 
 * @retval TM1638_Result_t
 *         - TM1638_OK: Operation was successful
 */
TM1638_Result_t
TM1638_ScanKeys(bDriverInterface_t *pdrv, uint32_t *Keys)
{
  uint8_t KeyRegs[4];
  uint32_t KeysBuff = 0;
  uint8_t Kn = 0x01;

  TM1638_ScanKeyRegs(pdrv, KeyRegs);

  for (uint8_t i = 0; i < 3; i++)
  {
    for (int8_t i = 3; i >= 0; i--)
    {
      KeysBuff <<= 1;

      if (KeyRegs[i] & (Kn << 4))
        KeysBuff |= 1;

      KeysBuff <<= 1;

      if (KeyRegs[i] & Kn)
        KeysBuff |= 1;
    }

    Kn <<= 1;
  }

  *Keys = KeysBuff;

  return TM1638_OK;
}

/**
 * \}
 */
static int _bTm1638Write(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
	bDRIVER_GET_PRIVATE(_priv, bTm1638Private_t, pdrv);
	if(_priv->Tm1638MultipleDigit == B_TM1638_MULTIPLE_DIGIT_HEX)
	{
		TM1638_SetMultipleDigit_HEX(pdrv, pbuf, off, len);
	}
	else if(_priv->Tm1638MultipleDigit == B_TM1638_MULTIPLE_DIGIT_CHAR)
	{
		TM1638_SetMultipleDigit_CHAR(pdrv, pbuf,
								off, len);
	}
	else if(_priv->Tm1638MultipleDigit == B_TM1638_MULTIPLE_DIGIT_LED)
	{
		
	}
	return len;	
}
static int _bTm1638Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{

	return len;
}
static int _bTm1638Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
	bDRIVER_GET_PRIVATE(_priv, bTm1638Private_t, pdrv);
	switch (cmd)
	{
		case bCMD_TM1638_SET_DIGIT_TYPE :
		{
			bTm1638MultipleDigit_t *pMultipleDigit = (bTm1638MultipleDigit_t *)param;
			_priv->Tm1638MultipleDigit = *pMultipleDigit;
		}
			break;
		default:
			break;	
	}
	
	return 0;
}
/**
 * \addtogroup TM1638_Exported_Functions
 * \{
 */

int bTM1638_Init(bDriverInterface_t *pdrv)
{
	int retval = -1;
	bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bTM1638_Init);
	pdrv->write       = _bTm1638Write;
	pdrv->read        = _bTm1638Read;
	pdrv->ctl         = _bTm1638Ctl;
	pdrv->_private._p = &bTm1638RunInfo[pdrv->drv_no];
	memset(pdrv->_private._p, 0, sizeof(bTm1638Private_t));
	
	TM1638_Init(pdrv, TM1638DisplayTypeComCathode);
	retval = TM1638_ConfigDisplay(pdrv, 1, TM1638DisplayStateON);
	
	bTm1638RunInfo[pdrv->drv_no].Tm1638MultipleDigit = B_TM1638_MULTIPLE_DIGIT_CHAR;
	

	uint8_t Buffer[8] = {' ',' ',' ',' ',' ',' ',' ',' '};
//	Buffer[0] = i % 10;
//	Buffer[1] = (i / 10) % 10;
//	Buffer[2] = (i / 100) % 10;
//	Buffer[3] = (i / 1000) % 10;
//	uint8_t Buffer[16] = {4,0,3,0,2,0,1,0,1,0,2,0,3,0,4,0};

//	Buffer[0] |= TM1638DecimalPoint;

//	TM1638_SetMultipleDigit_HEX(pdrv, Buffer, 0, 16);

	TM1638_SetMultipleDigit_CHAR(pdrv, Buffer, 0, 8);

	return retval;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_TM1638, bTM1638_Init);
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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
