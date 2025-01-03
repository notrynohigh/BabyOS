/**
 *!
 * \file        b_drv_bq769x2.c
 * \version     v0.0.1
 * \date        2024/12/30
 * \author      hmchen(chenhaimeng@189.cn)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 hmchen
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
#include "drivers/inc/b_drv_bq769x2.h"
#include "drivers/inc/BQ769x2Header.h"
#include <string.h>

#include "utils/inc/b_util_log.h"

//#include "main.h"
/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup BQ769X2
 * \{
 */

/**
 * \defgroup BQ769X2_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup BQ769X2_Private_Defines
 * \{
 */
#define DRIVER_NAME BQ769X2


/**
 * \}
 */

/**
 * \defgroup BQ769X2_Private_Macros
 * \{
 */
#define DEV_ADDR  0x10  // BQ769x2 address is 0x10 including R/W bit or 0x8 as 7-bit address
#define CRC_Mode 0  // 0 for disabled, 1 for enabled
#define MAX_BUFFER_SIZE 10
#define R 0 // Read; Used in DirectCommands and Subcommands functions
#define W 1 // Write; Used in DirectCommands and Subcommands functions
#define W2 2 // Write data with two bytes; Used in Subcommands function
/**
 * \}
 */

/**
 * \defgroup BQ769X2_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bBQ769X2_HalIf_t, DRIVER_NAME);
static bBQ769X2Private_t bBQ769X2RunInfo[bDRIVER_HALIF_NUM(bBQ769X2_HalIf_t, DRIVER_NAME)];

/**
 * \}
 */
// extern TIM_HandleTypeDef htim1;
//void bHalDelayUs(uint32_t us) {   // Sets the delay in microseconds.
//	__HAL_TIM_SET_COUNTER(&htim1,0);  // set the counter value a 0
//	while (__HAL_TIM_GET_COUNTER(&htim1) < us);  // wait for the counter to reach the us input in the parameter
//}
/**
 * \defgroup BQ769X2_Private_FunctionPrototypes
 * \{
 */
uint8_t RX_data [2] = {0x00, 0x00}; // used in several functions to store data read from BQ769x2
uint8_t RX_32Byte [32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	//used in Subcommands read function
	
// Global Variables for cell voltages, temperatures, Stack voltage, PACK Pin voltage, LD Pin voltage, CC2 current
uint16_t CellVoltage [16] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
float Temperature [3] = {0,0,0};
uint16_t Stack_Voltage = 0x00;
uint16_t Pack_Voltage = 0x00;
uint16_t LD_Voltage = 0x00;
uint16_t Pack_Current = 0x00;

uint16_t AlarmBits = 0x00;
uint8_t value_SafetyStatusA;  // Safety Status Register A
uint8_t value_SafetyStatusB;  // Safety Status Register B
uint8_t value_SafetyStatusC;  // Safety Status Register C
uint8_t value_PFStatusA;   // Permanent Fail Status Register A
uint8_t value_PFStatusB;   // Permanent Fail Status Register B
uint8_t value_PFStatusC;   // Permanent Fail Status Register C
uint8_t FET_Status;  // FET Status register contents  - Shows states of FETs
uint16_t CB_ActiveCells;  // Cell Balancing Active Cells

uint8_t	UV_Fault = 0;   // under-voltage fault state
uint8_t	OV_Fault = 0;   // over-voltage fault state
uint8_t	SCD_Fault = 0;  // short-circuit fault state
uint8_t	OCD_Fault = 0;  // over-current fault state
uint8_t ProtectionsTriggered = 0; // Set to 1 if any protection triggers

uint8_t LD_ON = 0;	// Load Detect status bit
uint8_t DSG = 0;   // discharge FET state
uint8_t CHG = 0;   // charge FET state
uint8_t PCHG = 0;  // pre-charge FET state
uint8_t PDSG = 0;  // pre-discharge FET state

uint32_t AccumulatedCharge_Int; // in BQ769x2_READPASSQ func
uint32_t AccumulatedCharge_Frac;// in BQ769x2_READPASSQ func
uint32_t AccumulatedCharge_Time;// in BQ769x2_READPASSQ func

/**
 * \}
 */

/**
 * \defgroup BQ769X2_Private_Functions
 * \{
 */
 
#if CRC_Mode 
static void CopyArray(uint8_t *source, uint8_t *dest, uint8_t count)
{
    uint8_t copyIndex = 0;
    for (copyIndex = 0; copyIndex < count; copyIndex++)
    {
        dest[copyIndex] = source[copyIndex];
    }
}
#endif

static unsigned char Checksum(unsigned char *ptr, unsigned char len)
// Calculates the checksum when writing to a RAM register. The checksum is the inverse of the sum of the bytes.	
{
	unsigned char i;
	unsigned char checksum = 0;

	for(i=0; i<len; i++)
		checksum += ptr[i];

	checksum = 0xff & ~checksum;

	return(checksum);
}

#if CRC_Mode 
static unsigned char CRC8(unsigned char *ptr, unsigned char len)
//Calculates CRC8 for passed bytes. Used in i2c read and write functions 
{
	unsigned char i;
	unsigned char crc=0;
	while(len--!=0)
	{
		for(i=0x80; i!=0; i/=2)
		{
			if((crc & 0x80) != 0)
			{
				crc *= 2;
				crc ^= 0x107;
			}
			else
				crc *= 2;

			if((*ptr & i)!=0)
				crc ^= 0x107;
		}
		ptr++;
	}
	return(crc);
}
#endif

static void I2C_WriteReg(bDriverInterface_t *pdrv,uint8_t reg_addr, uint8_t *reg_data, uint8_t count)
{
	bDRIVER_GET_HALIF(_if, bBQ769X2_HalIf_t, pdrv);

#if CRC_Mode
	{
		uint8_t TX_Buffer [MAX_BUFFER_SIZE] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
		uint8_t crc_count = 0;
		crc_count = count * 2;
		uint8_t crc1stByteBuffer [3] = {0x10, reg_addr, reg_data[0]};
		unsigned int j;
		unsigned int i;
		uint8_t temp_crc_buffer [3];

		TX_Buffer[0] = reg_data[0];
		TX_Buffer[1] = CRC8(crc1stByteBuffer,3);

		j = 2;
		for(i=1; i<count; i++)
		{
			TX_Buffer[j] = reg_data[i];
			j = j + 1;
			temp_crc_buffer[0] = reg_data[i];
			TX_Buffer[j] = CRC8(temp_crc_buffer,1);
			j = j + 1;
		}
		
		bHalI2CMemWrite(_if, reg_addr, 1, TX_Buffer, crc_count);
	}
#else 
	bHalI2CMemWrite(_if, reg_addr, 1, reg_data, count);
#endif
}

static int I2C_ReadReg(bDriverInterface_t *pdrv,uint8_t reg_addr, uint8_t *reg_data, uint8_t count)
{
	bDRIVER_GET_HALIF(_if, bBQ769X2_HalIf_t, pdrv);

#if CRC_Mode
	{
		unsigned int RX_CRC_Fail = 0;  // reset to 0. If in CRC Mode and CRC fails, this will be incremented.
		uint8_t RX_Buffer [MAX_BUFFER_SIZE] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
		uint8_t crc_count = 0;
		uint8_t ReceiveBuffer [10] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
		crc_count = count * 2;
		unsigned int j;
		unsigned int i;
		unsigned char CRCc = 0;
		uint8_t temp_crc_buffer [3];

		bHalI2CMemRead(_if, reg_addr, 1, ReceiveBuffer, crc_count);
		uint8_t crc1stByteBuffer [4] = {0x10, reg_addr, 0x11, ReceiveBuffer[0]};
		CRCc = CRC8(crc1stByteBuffer,4);
		if (CRCc != ReceiveBuffer[1])
		{
			RX_CRC_Fail += 1;
		}
		RX_Buffer[0] = ReceiveBuffer[0];

		j = 2;
		for (i=1; i<count; i++)
		{
			RX_Buffer[i] = ReceiveBuffer[j];
			temp_crc_buffer[0] = ReceiveBuffer[j];
			j = j + 1;
			CRCc = CRC8(temp_crc_buffer,1);
			if (CRCc != ReceiveBuffer[j])
				RX_CRC_Fail += 1;
			j = j + 1;
		}
		CopyArray(RX_Buffer, reg_data, crc_count);
	}
#else
	bHalI2CMemRead(_if, reg_addr, 1, reg_data, count);
#endif
	return 0;
}

static void BQ769x2_SetRegister(bDriverInterface_t *pdrv,uint16_t reg_addr, uint32_t reg_data, uint8_t datalen)
{
	uint8_t TX_Buffer[2] = {0x00, 0x00};
	uint8_t TX_RegData[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	//TX_RegData in little endian format
	TX_RegData[0] = reg_addr & 0xff; 
	TX_RegData[1] = (reg_addr >> 8) & 0xff;
	TX_RegData[2] = reg_data & 0xff; //1st byte of data

	switch(datalen)
    {
		case 1: //1 byte datalength
      		I2C_WriteReg(pdrv, 0x3E, TX_RegData, 3);
			bHalDelayUs(2000);
			TX_Buffer[0] = Checksum(TX_RegData, 3); 
			TX_Buffer[1] = 0x05; //combined length of register address and data
      		I2C_WriteReg(pdrv, 0x60, TX_Buffer, 2); // Write the checksum and length
			bHalDelayUs(2000);
			break;
		case 2: //2 byte datalength
			TX_RegData[3] = (reg_data >> 8) & 0xff;
			I2C_WriteReg(pdrv, 0x3E, TX_RegData, 4);
			bHalDelayUs(2000);
			TX_Buffer[0] = Checksum(TX_RegData, 4); 
			TX_Buffer[1] = 0x06; //combined length of register address and data
      		I2C_WriteReg(pdrv, 0x60, TX_Buffer, 2); // Write the checksum and length
			bHalDelayUs(2000);
			break;
		case 4: //4 byte datalength, Only used for CCGain and Capacity Gain
			TX_RegData[3] = (reg_data >> 8) & 0xff;
			TX_RegData[4] = (reg_data >> 16) & 0xff;
			TX_RegData[5] = (reg_data >> 24) & 0xff;
			I2C_WriteReg(pdrv, 0x3E, TX_RegData, 6);
			bHalDelayUs(2000);
			TX_Buffer[0] = Checksum(TX_RegData, 6); 
			TX_Buffer[1] = 0x08; //combined length of register address and data
      		I2C_WriteReg(pdrv, 0x60, TX_Buffer, 2); // Write the checksum and length
			bHalDelayUs(2000);
			break;
    }
}

static void CommandSubcommands(bDriverInterface_t *pdrv,uint16_t command) //For Command only Subcommands
// See the TRM or the BQ76952 header file for a full list of Command-only subcommands
{	//For DEEPSLEEP/SHUTDOWN subcommand you will need to call this function twice consecutively
	uint8_t TX_Reg[2] = {0x00, 0x00};

	//TX_Reg in little endian format
	TX_Reg[0] = command & 0xff;
	TX_Reg[1] = (command >> 8) & 0xff;

	I2C_WriteReg(pdrv, 0x3E,TX_Reg,2); 
	bHalDelayUs(2000);
}

static void Subcommands(bDriverInterface_t *pdrv,uint16_t command, uint16_t data, uint8_t type)
// See the TRM or the BQ76952 header file for a full list of Subcommands
{
	//security keys and Manu_data writes dont work with this function (reading these commands works)
	//max readback size is 32 bytes i.e. DASTATUS, CUV/COV snapshot
	uint8_t TX_Reg[4] = {0x00, 0x00, 0x00, 0x00};
	uint8_t TX_Buffer[2] = {0x00, 0x00};

	//TX_Reg in little endian format
	TX_Reg[0] = command & 0xff;
	TX_Reg[1] = (command >> 8) & 0xff; 

	if (type == R) {//read
		I2C_WriteReg(pdrv, 0x3E,TX_Reg,2);
		bHalDelayUs(2000);
		I2C_ReadReg(pdrv, 0x40, RX_32Byte, 32); //RX_32Byte is a global variable
	}
	else if (type == W) {
		//FET_Control, REG12_Control
		TX_Reg[2] = data & 0xff; 
		I2C_WriteReg(pdrv, 0x3E,TX_Reg,3);
		bHalDelayUs(1000);
		TX_Buffer[0] = Checksum(TX_Reg, 3);
		TX_Buffer[1] = 0x05; //combined length of registers address and data
		I2C_WriteReg(pdrv, 0x60, TX_Buffer, 2);
		bHalDelayUs(1000); 
	}
	else if (type == W2){ //write data with 2 bytes
		//CB_Active_Cells, CB_SET_LVL
		TX_Reg[2] = data & 0xff; 
		TX_Reg[3] = (data >> 8) & 0xff;
		I2C_WriteReg(pdrv, 0x3E,TX_Reg,4);
		bHalDelayUs(1000);
		TX_Buffer[0] = Checksum(TX_Reg, 4); 
		TX_Buffer[1] = 0x06; //combined length of registers address and data
		I2C_WriteReg(pdrv, 0x60, TX_Buffer, 2);
		bHalDelayUs(1000); 
	}
}

static void DirectCommands(bDriverInterface_t *pdrv,uint8_t command, uint16_t data, uint8_t type)
// See the TRM or the BQ76952 header file for a full list of Direct Commands
{	//type: R = read, W = write
	uint8_t TX_data[2] = {0x00, 0x00};

	//little endian format
	TX_data[0] = data & 0xff;
	TX_data[1] = (data >> 8) & 0xff;

	if (type == R) {//Read
		I2C_ReadReg(pdrv, command, RX_data, 2); //RX_data is a global variable
		bHalDelayUs(2000);
	}
	if (type == W) {//write
    //Control_status, alarm_status, alarm_enable all 2 bytes long
		I2C_WriteReg(pdrv, command,TX_data,2);
		bHalDelayUs(2000);
	}
}

//  ********************************* FET Control Commands  ***************************************

void BQ769x2_BOTHOFF () {
	// Disables all FETs using the DFETOFF (BOTHOFF) pin
	// The DFETOFF pin on the BQ76952EVM should be connected to the MCU board to use this function
//	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);  // DFETOFF pin (BOTHOFF) set high
}

void BQ769x2_RESET_BOTHOFF () {
	// Resets DFETOFF (BOTHOFF) pin
	// The DFETOFF pin on the BQ76952EVM should be connected to the MCU board to use this function
//	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);  // DFETOFF pin (BOTHOFF) set low
}

void BQ769x2_ReadFETStatus(bDriverInterface_t *pdrv) { 
	// Read FET Status to see which FETs are enabled
	DirectCommands(pdrv, FETStatus, 0x00, R);
	FET_Status = (RX_data[1]*256 + RX_data[0]);
	DSG = ((0x4 & RX_data[0])>>2);// discharge FET state
  	CHG = (0x1 & RX_data[0]);// charge FET state
  	PCHG = ((0x2 & RX_data[0])>>1);// pre-charge FET state
  	PDSG = ((0x8 & RX_data[0])>>3);// pre-discharge FET state
}

// ********************************* End of FET Control Commands *********************************

// ********************************* BQ769x2 Power Commands   *****************************************

void BQ769x2_ShutdownPin() {
	// Puts the device into SHUTDOWN mode using the RST_SHUT pin
	// The RST_SHUT pin on the BQ76952EVM should be connected to the MCU board to use this function	
//	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);  // Sets RST_SHUT pin
}

void BQ769x2_ReleaseShutdownPin() {
	// Releases the RST_SHUT pin
	// The RST_SHUT pin on the BQ76952EVM should be connected to the MCU board to use this function	
//	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);  // Resets RST_SHUT pin
}

// ********************************* End of BQ769x2 Power Commands   *****************************************


// ********************************* BQ769x2 Status and Fault Commands   *****************************************

uint16_t BQ769x2_ReadAlarmStatus(bDriverInterface_t *pdrv) { 
	// Read this register to find out why the ALERT pin was asserted
	DirectCommands(pdrv, AlarmStatus, 0x00, R);
	return (RX_data[1]*256 + RX_data[0]);
}

void BQ769x2_ReadSafetyStatus(bDriverInterface_t *pdrv) { //good example functions
	// Read Safety Status A/B/C and find which bits are set
	// This shows which primary protections have been triggered
	DirectCommands(pdrv, SafetyStatusA, 0x00, R);
	value_SafetyStatusA = (RX_data[1]*256 + RX_data[0]);
	//Example Fault Flags
	UV_Fault = ((0x4 & RX_data[0])>>2); 
	OV_Fault = ((0x8 & RX_data[0])>>3);
	SCD_Fault = ((0x8 & RX_data[1])>>3);
	OCD_Fault = ((0x2 & RX_data[1])>>1);
	DirectCommands(pdrv, SafetyStatusB, 0x00, R);
	value_SafetyStatusB = (RX_data[1]*256 + RX_data[0]);
	DirectCommands(pdrv, SafetyStatusC, 0x00, R);
	value_SafetyStatusC = (RX_data[1]*256 + RX_data[0]);
	if ((value_SafetyStatusA + value_SafetyStatusB + value_SafetyStatusC) > 1) {
		ProtectionsTriggered = 1; }
	else {
		ProtectionsTriggered = 0; }
}

void BQ769x2_ReadPFStatus(bDriverInterface_t *pdrv) {
	// Read Permanent Fail Status A/B/C and find which bits are set
	// This shows which permanent failures have been triggered
	DirectCommands(pdrv, PFStatusA, 0x00, R);
	value_PFStatusA = (RX_data[1]*256 + RX_data[0]);
	DirectCommands(pdrv, PFStatusB, 0x00, R);
	value_PFStatusB = (RX_data[1]*256 + RX_data[0]);
	DirectCommands(pdrv, PFStatusC, 0x00, R);
	value_PFStatusC = (RX_data[1]*256 + RX_data[0]);
}

// ********************************* End of BQ769x2 Status and Fault Commands   *****************************************


// ********************************* BQ769x2 Measurement Commands   *****************************************


uint16_t BQ769x2_ReadVoltage(bDriverInterface_t *pdrv, uint8_t command)
// This function can be used to read a specific cell voltage or stack / pack / LD voltage
{
	//RX_data is global var
	DirectCommands(pdrv, command, 0x00, R);
	if(command >= Cell1Voltage && command <= Cell16Voltage) {//Cells 1 through 16 (0x14 to 0x32)
		return (RX_data[1]*256 + RX_data[0]); //voltage is reported in mV
	}
	else {//stack, Pack, LD
		return 10 * (RX_data[1]*256 + RX_data[0]); //voltage is reported in 0.01V units
	}

}
void BQ769x2_ReadAllVoltages(bDriverInterface_t *pdrv)
// Reads all cell voltages, Stack voltage, PACK pin voltage, and LD pin voltage
{
  int cellvoltageholder = Cell1Voltage; //Cell1Voltage is 0x14
  for (int x = 0; x < 16; x++){//Reads all cell voltages
    CellVoltage[x] = BQ769x2_ReadVoltage(pdrv, cellvoltageholder);
    cellvoltageholder = cellvoltageholder + 2;
  }
  Stack_Voltage = BQ769x2_ReadVoltage(pdrv, StackVoltage);
  Pack_Voltage = BQ769x2_ReadVoltage(pdrv, PACKPinVoltage);
  LD_Voltage = BQ769x2_ReadVoltage(pdrv, LDPinVoltage);
}

uint16_t BQ769x2_ReadCurrent(bDriverInterface_t *pdrv) 
// Reads PACK current 
{
	DirectCommands(pdrv, CC2Current, 0x00, R);
	return (RX_data[1]*256 + RX_data[0]);  // current is reported in mA
}

float BQ769x2_ReadTemperature(bDriverInterface_t *pdrv, uint8_t command) 
{
	DirectCommands(pdrv, command, 0x00, R);
	//RX_data is a global var
	return (0.1 * (float)(RX_data[1]*256 + RX_data[0])) - 273.15;  // converts from 0.1K to Celcius
}

void BQ769x2_ReadPassQ(bDriverInterface_t *pdrv){ // Read Accumulated Charge and Time from DASTATUS6 
	Subcommands(pdrv, DASTATUS6, 0x00, R);
	AccumulatedCharge_Int = ((RX_32Byte[3]<<24) + (RX_32Byte[2]<<16) + (RX_32Byte[1]<<8) + RX_32Byte[0]); //Bytes 0-3
	AccumulatedCharge_Frac = ((RX_32Byte[7]<<24) + (RX_32Byte[6]<<16) + (RX_32Byte[5]<<8) + RX_32Byte[4]); //Bytes 4-7
	AccumulatedCharge_Time = ((RX_32Byte[11]<<24) + (RX_32Byte[10]<<16) + (RX_32Byte[9]<<8) + RX_32Byte[8]); //Bytes 8-11
}

// ********************************* End of BQ769x2 Measurement Commands   *****************************************




void BQ769x2_Init(bDriverInterface_t *pdrv) {
	// Configures all parameters in device RAM

	// Enter CONFIGUPDATE mode (Subcommand 0x0090) - It is required to be in CONFIG_UPDATE mode to program the device RAM settings
	// See TRM for full description of CONFIG_UPDATE mode
	CommandSubcommands(pdrv, SET_CFGUPDATE);

	// After entering CONFIG_UPDATE mode, RAM registers can be programmed. When programming RAM, checksum and length must also be
	// programmed for the change to take effect. All of the RAM registers are described in detail in the BQ769x2 TRM.
	// An easier way to find the descriptions is in the BQStudio Data Memory screen. When you move the mouse over the register name,
	// a full description of the register and the bits will pop up on the screen.

	// 'Power Config' - 0x9234 = 0x2D80
	// Setting the DSLP_LDO bit allows the LDOs to remain active when the device goes into Deep Sleep mode
  	// Set wake speed bits to 00 for best performance
	BQ769x2_SetRegister(pdrv, PowerConfig, 0x2D80, 2);

	// 'REG0 Config' - set REG0_EN bit to enable pre-regulator
	BQ769x2_SetRegister(pdrv, REG0Config, 0x01, 1);

	// 'REG12 Config' - Enable REG1 with 3.3V output (0x0D for 3.3V, 0x0F for 5V)
	BQ769x2_SetRegister(pdrv, REG12Config, 0x0D, 1);

	// Set DFETOFF pin to control BOTH CHG and DSG FET - 0x92FB = 0x42 (set to 0x00 to disable)
	BQ769x2_SetRegister(pdrv, DFETOFFPinConfig, 0x42, 1);

	// Set up ALERT Pin - 0x92FC = 0x2A
	// This configures the ALERT pin to drive high (REG1 voltage) when enabled.
	// The ALERT pin can be used as an interrupt to the MCU when a protection has triggered or new measurements are available
	BQ769x2_SetRegister(pdrv, ALERTPinConfig, 0x2A, 1);

	// Set TS1 to measure Cell Temperature - 0x92FD = 0x07
	BQ769x2_SetRegister(pdrv, TS1Config, 0x07, 1);

	// Set TS3 to measure FET Temperature - 0x92FF = 0x0F
	BQ769x2_SetRegister(pdrv, TS3Config, 0x0F, 1);

	// Set HDQ to measure Cell Temperature - 0x9300 = 0x07
	BQ769x2_SetRegister(pdrv, HDQPinConfig, 0x00, 1);  // No thermistor installed on EVM HDQ pin, so set to 0x00

	// 'VCell Mode' - Enable 16 cells - 0x9304 = 0x0000; Writing 0x0000 sets the default of 16 cells
	BQ769x2_SetRegister(pdrv, VCellMode, 0x0000, 2);

	// Enable protections in 'Enabled Protections A' 0x9261 = 0xBC
	// Enables SCD (short-circuit), OCD1 (over-current in discharge), OCC (over-current in charge),
	// COV (over-voltage), CUV (under-voltage)
	BQ769x2_SetRegister(pdrv, EnabledProtectionsA, 0xBC, 1);

	// Enable all protections in 'Enabled Protections B' 0x9262 = 0xF7
	// Enables OTF (over-temperature FET), OTINT (internal over-temperature), OTD (over-temperature in discharge),
	// OTC (over-temperature in charge), UTINT (internal under-temperature), UTD (under-temperature in discharge), UTC (under-temperature in charge)
	BQ769x2_SetRegister(pdrv, EnabledProtectionsB, 0xF7, 1);

	// 'Default Alarm Mask' - 0x..82 Enables the FullScan and ADScan bits, default value = 0xF800
	BQ769x2_SetRegister(pdrv, DefaultAlarmMask, 0xF882, 2);

	// Set up Cell Balancing Configuration - 0x9335 = 0x03   -  Automated balancing while in Relax or Charge modes
	// Also see "Cell Balancing with BQ769x2 Battery Monitors" document on ti.com
	BQ769x2_SetRegister(pdrv, BalancingConfiguration, 0x03, 1);

	// Set up CUV (under-voltage) Threshold - 0x9275 = 0x31 (2479 mV)
	// CUV Threshold is this value multiplied by 50.6mV
	BQ769x2_SetRegister(pdrv, CUVThreshold, 0x31, 1);

	// Set up COV (over-voltage) Threshold - 0x9278 = 0x55 (4301 mV)
	// COV Threshold is this value multiplied by 50.6mV
	BQ769x2_SetRegister(pdrv, COVThreshold, 0x55, 1);

	// Set up OCC (over-current in charge) Threshold - 0x9280 = 0x05 (10 mV = 10A across 1mOhm sense resistor) Units in 2mV
	BQ769x2_SetRegister(pdrv, OCCThreshold, 0x05, 1);

	// Set up OCD1 Threshold - 0x9282 = 0x0A (20 mV = 20A across 1mOhm sense resistor) units of 2mV
	BQ769x2_SetRegister(pdrv, OCD1Threshold, 0x0A, 1);

	// Set up SCD Threshold - 0x9286 = 0x05 (100 mV = 100A across 1mOhm sense resistor)  0x05=100mV
	BQ769x2_SetRegister(pdrv, SCDThreshold, 0x05, 1);

	// Set up SCD Delay - 0x9287 = 0x03 (30 us) Enabled with a delay of (value - 1) * 15 µs; min value of 1    
	BQ769x2_SetRegister(pdrv, SCDDelay, 0x03, 1);

	// Set up SCDL Latch Limit to 1 to set SCD recovery only with load removal 0x9295 = 0x01
	// If this is not set, then SCD will recover based on time (SCD Recovery Time parameter).
	BQ769x2_SetRegister(pdrv, SCDLLatchLimit, 0x01, 1);

	// Exit CONFIGUPDATE mode  - Subcommand 0x0092
	CommandSubcommands(pdrv, EXIT_CFGUPDATE);
}

static int _bBQ769X2Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_PRIVATE(_priv, bBQ769X2Private_t, pdrv);
	if (pbuf == NULL || len < sizeof(bBMS_AFE_BQ769X2_Value_t))
    {
        return -1;
    }
	_priv->value.AlarmBits = BQ769x2_ReadAlarmStatus(pdrv);
	if (_priv->value.AlarmBits & 0x80) {  // Check if FULLSCAN is complete. If set, new measurements are available
		BQ769x2_ReadAllVoltages(pdrv);
		Pack_Current = BQ769x2_ReadCurrent(pdrv);
		Temperature[0] = BQ769x2_ReadTemperature(pdrv,TS1Temperature);
		Temperature[1] = BQ769x2_ReadTemperature(pdrv,TS3Temperature);
		DirectCommands(pdrv, AlarmStatus, 0x0080, W);  // Clear the FULLSCAN bit
	}
	
    bBMS_AFE_BQ769X2_Value_t *pval = (bBMS_AFE_BQ769X2_Value_t *)pbuf;
	pval->AlarmBits = _priv->value.AlarmBits;
	
	for (int x = 0; x < 16; x++){//Reads all cell voltages
	pval->CellVoltage[x] = CellVoltage[x];
	}
	pval->Temperature[0] = Temperature[0];
	pval->Temperature[1] = Temperature[1];
	
    return sizeof(bBMS_AFE_BQ769X2_Value_t);
}

static int _bBQ769X2Open(bDriverInterface_t *pdrv)
{
    return 0;
}

static int _bBQ769X2Close(bDriverInterface_t *pdrv)
{
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup BQ769X2_Exported_Functions
 * \{
 */
int bBQ769X2_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bBQ769X2_Init);
    pdrv->read        = _bBQ769X2Read;  // read: bBMS_AFE_BQ769X2_Value_t
    pdrv->write       = NULL;
    pdrv->ctl         = NULL;
    pdrv->open        = _bBQ769X2Open;
    pdrv->close       = _bBQ769X2Close;
    pdrv->_private._p = &bBQ769X2RunInfo[pdrv->drv_no];
    memset(&bBQ769X2RunInfo[pdrv->drv_no], 0, sizeof(bBQ769X2Private_t));
	bHalDelayMs(50);
	CommandSubcommands(pdrv, BQ769x2_RESET);  // Resets the BQ769x2 registers
	bHalDelayUs(60000);
	BQ769x2_Init(pdrv);  // Configure all of the BQ769x2 register settings
	bHalDelayUs(10000);
	CommandSubcommands(pdrv,FET_ENABLE); // Enable the CHG and DSG FETs
	bHalDelayUs(10000);
	CommandSubcommands(pdrv, SLEEP_DISABLE); // Sleep mode is enabled by default. For this example, Sleep is disabled to 
									   // demonstrate full-speed measurements in Normal mode. 

	bHalDelayUs(60000); bHalDelayUs(60000); bHalDelayUs(60000); bHalDelayUs(60000);  //wait to start measurements after FETs close

    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_BQ769X2, bBQ769X2_Init);
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

/************************ Copyright (c) 2024 hmchen *****END OF FILE****/
