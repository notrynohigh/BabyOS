/**
 *!
 * \file        b_drv_ads124x.c
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
#include "drivers/inc/b_drv_ads124x.h"
#include "drivers/inc/ADS124x_regs.h"
#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup HLW8112
 * \{
 */

/**
 * \defgroup HLW8112_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup HLW8112_Private_Defines
 * \{
 */

#define DRIVER_NAME ADS124X

/** Device Identification (Who am I) **/

/**
 * \}
 */

/**
 * \defgroup ADS124X_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ADS124X_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bADS124X_HalIf_t, DRIVER_NAME);

//static bAds124xPrivate_t bAds124xRunInfo[bDRIVER_HALIF_NUM(bADS124X_HalIf_t, DRIVER_NAME)];


/* Private Constants ------------------------------------------------------------*/
/**
 * @brief  Commands
 */



/*
 * DRDY Polling Function
 * Timeout = 0 is wait until DRDY goes low no matter how long it takes, otherwise wait the specified number of cycles
 */
int ADS1248WaitForDataReady(bDriverInterface_t *pdrv, int Timeout)
{
	/* This function shows a method for polling DRDY instead of using as interrupt function
	 * -- Note: this method is not used in the demo, but if we the method was switched to polling from the interrupt method,
	 * the desired port is PORT2 on the MSP430 as this demo is configured.
	 */
	bDRIVER_GET_HALIF(_if, bADS124X_HalIf_t, pdrv);
	
	if (Timeout > 0)
	{
		// wait for /DRDY = 1 to make sure it is high before we look for the transition low	
		while (!(bHalGpioReadPin(_if->drdy.port, _if->drdy.pin)) && (Timeout-- >= 0));
		// wait for /DRDY = 0
		while ( (bHalGpioReadPin(_if->drdy.port, _if->drdy.pin)) && (Timeout-- >= 0));
		if (Timeout < 0)
			return ADS1248_ERROR; 					//ADS1248_TIMEOUT_WARNING;
	}
	else
	{
		// wait for /DRDY = 1
		while (!(bHalGpioReadPin(_if->drdy.port, _if->drdy.pin)));
		// wait for /DRDY = 0
		while ( (bHalGpioReadPin(_if->drdy.port, _if->drdy.pin)));
	}
	return ADS1248_NO_ERROR;
}

/*
 * Primary Low Level Functions
 */
void ADS1248AssertCS(bDriverInterface_t *pdrv, int fAssert)
{
	bDRIVER_GET_HALIF(_if, bADS124X_HalIf_t, pdrv);
	if (fAssert){				// fAssert=0 is CS low, fAssert=1 is CS high
		bHalDelayUs(100);		// Must delay minimum of 7 tosc periods from last falling SCLK to rising CS
		bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 1);
	} else
		bHalGpioWritePin(_if->_spi.cs.port, _if->_spi.cs.pin, 0);
}

void ADS1248SendByte(bDriverInterface_t *pdrv,unsigned char Byte)
{
	bDRIVER_GET_HALIF(_if, bADS124X_HalIf_t, pdrv);	
	bHalSpiSend(&_if->_spi, &Byte, 1);
}

unsigned char ADS1248ReceiveByte(bDriverInterface_t *pdrv)
{
	bDRIVER_GET_HALIF(_if, bADS124X_HalIf_t, pdrv);
	unsigned char Result = 0;
	bHalSpiReceive(&_if->_spi, &Result, 1);

	return Result;
}

/*
 * ADS1248 Higher Level Functions and Commands
 */
void ADS1248SendWakeup(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_WAKEUP);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

void ADS1248SendSleep(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_SLEEP);

	/*
	 * CS must remain low for the device to remain asleep by command...otherwise bring START low by pin control
	 */
	return;
}

void ADS1248SendSync(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_SYNC);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

void ADS1248SendResetCommand(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_RESET);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

long ADS1248ReadData(bDriverInterface_t *pdrv)
{
	long Data;
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_RDATA);
	// get the conversion result
#ifdef ADS1148
	Data = ADS1248ReceiveByte(pdrv);
	Data = (Data << 8) | ADS1248ReceiveByte();
    // sign extend data if the MSB is high (16 to 32 bit sign extension)
	if (Data & 0x8000)
		Data |= 0xffff0000;
#else
	Data = ADS1248ReceiveByte(pdrv);
	Data = (Data << 8) | ADS1248ReceiveByte(pdrv);
	Data = (Data << 8) | ADS1248ReceiveByte(pdrv);
	// sign extend data if the MSB is high (24 to 32 bit sign extension)
	if (Data & 0x800000)
		Data |= 0xff000000;
#endif
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return Data;
}

void ADS1248ReadRegister(bDriverInterface_t *pdrv,int StartAddress, int NumRegs, unsigned * pData)
{
	int i;
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_RREG | (StartAddress & 0x0f));
	ADS1248SendByte(pdrv,(NumRegs-1) & 0x0f);
	// get the register content
	for (i=0; i< NumRegs; i++)
	{
		*pData++ = ADS1248ReceiveByte(pdrv);
	}
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

void ADS1248WriteRegister(bDriverInterface_t *pdrv,int StartAddress, int NumRegs, unsigned * pData)
{
	int i;
	// set the CS low
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_WREG | (StartAddress & 0x0f));
	ADS1248SendByte(pdrv,(NumRegs-1) & 0x0f);
	// send the data bytes
	for (i=0; i < NumRegs; i++)
	{
		ADS1248SendByte(pdrv,*pData++);
	}
	// set the CS back high
	ADS1248AssertCS(pdrv,1);
}

void ADS1248WriteSequence(bDriverInterface_t *pdrv,
							int StartAddress, int NumRegs, unsigned * pData)
{
	bDRIVER_GET_HALIF(_if, bADS124X_HalIf_t, pdrv);	
	uint8_t Buffer[6] = {0};
	int i;
	
	// set the CS low
	ADS1248AssertCS(pdrv,0);
	if (NumRegs > 6)
		return ;	
	Buffer[0] = ADS1248_CMD_WREG | (StartAddress & 0x0f);
	Buffer[1] = (NumRegs-1) & 0x0f;
	
	for (i=2; i < NumRegs; i++)
	{
		Buffer[i] = *pData++;
	}
	bHalSpiSend(&_if->_spi, Buffer, sizeof(Buffer));	
	// set the CS back high
	ADS1248AssertCS(pdrv,1);
}

void ADS1248SendRDATAC(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_RDATAC);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

void ADS1248SendSDATAC(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_SDATAC);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

void ADS1248SendSYSOCAL(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_SYSOCAL);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

void ADS1248SendSYSGCAL(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_SYSGCAL);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

void ADS1248SendSELFOCAL(bDriverInterface_t *pdrv)
{
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// send the command byte
	ADS1248SendByte(pdrv,ADS1248_CMD_SELFOCAL);
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return;
}

/*
 * Register Set Value Commands
 *
 * These commands need to strip out old settings (AND) and add (OR) the new contents to the register
 */
int ADS1248SetBurnOutSource(bDriverInterface_t *pdrv,int BurnOut)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_0_MUX0, 0x01, &Temp);
	Temp &= 0x3f;
	switch(BurnOut) {
		case 0:
			Temp |= ADS1248_BCS_OFF;
			break;
		case 1:
			Temp |= ADS1248_BCS_500nA;
			break;
		case 2:
			Temp |= ADS1248_BCS_2uA;
			break;
		case 3:
			Temp |= ADS1248_BCS_10uA;
			break;
		default:
			dError = ADS1248_ERROR;
			Temp |= ADS1248_BCS_OFF;
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_0_MUX0, 0x01, &Temp);
	return dError;
}

int ADS1248SetChannel(bDriverInterface_t *pdrv,int vMux, int pMux)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_0_MUX0, 0x01, &Temp);
	if (pMux==1) {
		Temp &= 0xf8;
		switch(vMux) {
			case 0:
				Temp |= ADS1248_AINN0;
				break;
			case 1:
				Temp |= ADS1248_AINN1;
				break;
			case 2:
				Temp |= ADS1248_AINN2;
				break;
			case 3:
				Temp |= ADS1248_AINN3;
				break;
			case 4:
				Temp |= ADS1248_AINN4;
				break;
			case 5:
				Temp |= ADS1248_AINN5;
				break;
			case 6:
				Temp |= ADS1248_AINN6;
				break;
			case 7:
				Temp |= ADS1248_AINN7;
				break;
			default:
				Temp |= ADS1248_AINN0;
				dError = ADS1248_ERROR;
		}

	} else {
		Temp &= 0xc7;
		switch(vMux) {
			case 0:
				Temp |= ADS1248_AINP0;
				break;
			case 1:
				Temp |= ADS1248_AINP1;
				break;
			case 2:
				Temp |= ADS1248_AINP2;
				break;
			case 3:
				Temp |= ADS1248_AINP3;
				break;
			case 4:
				Temp |= ADS1248_AINP4;
				break;
			case 5:
				Temp |= ADS1248_AINP5;
				break;
			case 6:
				Temp |= ADS1248_AINP6;
				break;
			case 7:
				Temp |= ADS1248_AINP7;
				break;
			default:
				Temp |= ADS1248_AINP0;
				dError = ADS1248_ERROR;
		}
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_0_MUX0, 0x01, &Temp);
	return dError;
}

int ADS1248SetBias(bDriverInterface_t *pdrv,unsigned char vBias)
{
	unsigned Temp;
	Temp = ADS1248_VBIAS_OFF;
	if (vBias & 0x80)
		Temp |=  ADS1248_VBIAS7;
	if (vBias & 0x40)
		Temp |=  ADS1248_VBIAS6;
	if (vBias & 0x20)
		Temp |=  ADS1248_VBIAS5;
	if (vBias & 0x10)
		Temp |=  ADS1248_VBIAS4;
	if (vBias & 0x08)
		Temp |=  ADS1248_VBIAS3;
	if (vBias & 0x04)
		Temp |=  ADS1248_VBIAS2;
	if (vBias & 0x02)
		Temp |=  ADS1248_VBIAS1;
	if (vBias & 0x01)
		Temp |=  ADS1248_VBIAS0;
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_1_VBIAS, 0x01, &Temp);
	return ADS1248_NO_ERROR;
}

// Relate to Mux1
int ADS1248SetIntRef(bDriverInterface_t *pdrv,int sRef)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	Temp &= 0x1f;
	switch(sRef) {
		case 0:
			Temp |= ADS1248_INT_VREF_OFF;
			break;
		case 1:
			Temp |= ADS1248_INT_VREF_ON;
			break;
		case 2:
		case 3:
			Temp |= ADS1248_INT_VREF_CONV;
			break;
		default:
			Temp |= ADS1248_INT_VREF_OFF;
			dError = ADS1248_ERROR;

	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	return dError;
}

int ADS1248SetVoltageReference(bDriverInterface_t *pdrv,int VoltageRef)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	Temp &= 0xe7;
	switch(VoltageRef) {
		case 0:
			Temp |= ADS1248_REF0;
			break;
		case 1:
			Temp |= ADS1248_REF1;
			break;
		case 2:
			Temp |= ADS1248_INT;
			break;
		case 3:
			Temp |= ADS1248_INT_REF0;
			break;
		default:
			Temp |= ADS1248_REF0;
			dError = ADS1248_ERROR;
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	return dError;
}

int ADS1248SetSystemMonitor(bDriverInterface_t *pdrv,int Monitor)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	Temp &= 0x78;
	switch(Monitor) {
		case 0:
			Temp |= ADS1248_MEAS_NORM;
			break;
		case 1:
			Temp |= ADS1248_MEAS_OFFSET;
			break;
		case 2:
			Temp |= ADS1248_MEAS_GAIN;
			break;
		case 3:
			Temp |= ADS1248_MEAS_TEMP;
			break;
		case 4:
			Temp |= ADS1248_MEAS_REF1;
			break;
		case 5:
			Temp |= ADS1248_MEAS_REF0;
			break;
		case 6:
			Temp |= ADS1248_MEAS_AVDD;
			break;
		case 7:
			Temp |= ADS1248_MEAS_DVDD;
			break;
		default:
			Temp |= ADS1248_MEAS_NORM;
			dError = ADS1248_ERROR;
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	return dError;
}

// Relate to SYS0
int ADS1248SetGain(bDriverInterface_t *pdrv,int Gain)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_3_SYS0, 0x01, &Temp);
	Temp &= 0x0f;
	switch(Gain) {
		case 0:
			Temp |= ADS1248_GAIN_1;
			break;
		case 1:
			Temp |= ADS1248_GAIN_2;
			break;
		case 2:
			Temp |= ADS1248_GAIN_4;
			break;
		case 3:
			Temp |= ADS1248_GAIN_8;
			break;
		case 4:
			Temp |= ADS1248_GAIN_16;
			break;
		case 5:
			Temp |= ADS1248_GAIN_32;
			break;
		case 6:
			Temp |= ADS1248_GAIN_64;
			break;
		case 7:
			Temp |= ADS1248_GAIN_128;
			break;
		default:
			Temp |= ADS1248_GAIN_1;
			dError = ADS1248_ERROR;
		}
		// write the register value containing the new value back to the ADS
		ADS1248WriteRegister(pdrv,ADS1248_3_SYS0, 0x01, &Temp);
		return dError;
}

int ADS1248SetDataRate(bDriverInterface_t *pdrv,int DataRate)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_3_SYS0, 0x01, &Temp);
	Temp &= 0x70;
	switch(DataRate) {
		case 0:
			Temp |= ADS1248_DR_5;
			break;
		case 1:
			Temp |= ADS1248_DR_10;
			break;
		case 2:
			Temp |= ADS1248_DR_20;
			break;
		case 3:
			Temp |= ADS1248_DR_40;
			break;
		case 4:
			Temp |= ADS1248_DR_80;
			break;
		case 5:
			Temp |= ADS1248_DR_160;
			break;
		case 6:
			Temp |= ADS1248_DR_320;
			break;
		case 7:
			Temp |= ADS1248_DR_640;
			break;
		case 8:
			Temp |= ADS1248_DR_1000;
			break;
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
			Temp |= ADS1248_DR_2000;
			break;
		default:
			Temp |= ADS1248_DR_5;
			dError = ADS1248_ERROR;
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_3_SYS0, 0x01, &Temp);
	return dError;
}

// Relate to OFC (3 registers)
int ADS1248SetOFC(bDriverInterface_t *pdrv,long RegOffset)
{
	// find the pointer to the variable so we can write the value as bytes
	unsigned *cptr=(unsigned *)(&RegOffset);
	int i;

	for (i=0; i<3; i++)
	{
		// write the register value containing the new value back to the ADS
		ADS1248WriteRegister(pdrv,(ADS1248_4_OFC0 + i), 0x01, &cptr[i]);
	}
	return ADS1248_NO_ERROR;
}

// Relate to FSC (3 registers)
int ADS1248SetFSC(bDriverInterface_t *pdrv,long RegGain)
{
	// find the pointer to the variable so we can write the value as bytes
	unsigned *cptr=(unsigned *)(&RegGain);
	int i;
	for (i=0; i<3; i++)
	{
		// write the register value containing the new value back to the ADS
		ADS1248WriteRegister(pdrv,(ADS1248_7_FSC0 + i), 0x01, &cptr[i]);
	}
	return ADS1248_NO_ERROR;
}

// Relate to IDAC0
int ADS1248SetDRDYMode(bDriverInterface_t *pdrv,int DRDYMode)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_10_IDAC0, 0x01, &Temp);
	Temp &= 0xf7;
	switch(DRDYMode) {
		case 0:
			Temp |= ADS1248_DRDY_OFF;
			break;
		case 1:
			Temp |= ADS1248_DRDY_ON;
			break;
		default:
			Temp |= ADS1248_DRDY_OFF;
			dError = ADS1248_ERROR;
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_10_IDAC0, 0x01, &Temp);
	return dError;
}

int ADS1248SetCurrentDACOutput(bDriverInterface_t *pdrv,int CurrentOutput)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_10_IDAC0, 0x01, &Temp);
	Temp &= 0xf8;
	switch(CurrentOutput) {
		case 0:
			Temp |= ADS1248_IDAC_OFF;
			break;
		case 1:
			Temp |= ADS1248_IDAC_50;
			break;
		case 2:
			Temp |= ADS1248_IDAC_100;
			break;
		case 3:
			Temp |= ADS1248_IDAC_250;
			break;
		case 4:
			Temp |= ADS1248_IDAC_500;
			break;
		case 5:
			Temp |= ADS1248_IDAC_750;
			break;
		case 6:
			Temp |= ADS1248_IDAC_1000;
			break;
		case 7:
			Temp |= ADS1248_IDAC_1500;
			break;
		default:
			Temp |= ADS1248_IDAC_OFF;
			dError = ADS1248_ERROR;
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_10_IDAC0, 0x01, &Temp);
	return dError;
}

// Relate to IDAC1
int ADS1248SetIDACRouting(bDriverInterface_t *pdrv,int IDACroute, int IDACdir)		// IDACdir (0 = I1DIR, 1 = I2DIR)
{
	unsigned Temp;
	int dError = ADS1248_NO_ERROR;
	ADS1248ReadRegister(pdrv,ADS1248_11_IDAC1, 0x01, &Temp);
	if (IDACdir>0){
		Temp &= 0xf0;
		switch(IDACroute) {
			case 0:
				Temp |= ADS1248_IDAC2_A0;
				break;
			case 1:
				Temp |= ADS1248_IDAC2_A1;
				break;
			case 2:
				Temp |= ADS1248_IDAC2_A2;
				break;
			case 3:
				Temp |= ADS1248_IDAC2_A3;
				break;
			case 4:
				Temp |= ADS1248_IDAC2_A4;
				break;
			case 5:
				Temp |= ADS1248_IDAC2_A5;
				break;
			case 6:
				Temp |= ADS1248_IDAC2_A6;
				break;
			case 7:
				Temp |= ADS1248_IDAC2_A7;
				break;
			case 8:
				Temp |= ADS1248_IDAC2_EXT1;
				break;
			case 9:
				Temp |= ADS1248_IDAC2_EXT2;
				break;
			case 10:
				Temp |= ADS1248_IDAC2_EXT1;
				break;
			case 11:
				Temp |= ADS1248_IDAC2_EXT2;
				break;
			case 12:
			case 13:
			case 14:
			case 15:
				Temp |= ADS1248_IDAC2_OFF;
				break;
			default:
				Temp |= ADS1248_IDAC2_OFF;
				dError = ADS1248_ERROR;
		}

	} else {
		Temp &= 0x0f;
		switch(IDACroute) {
			case 0:
				Temp |= ADS1248_IDAC1_A0;
				break;
			case 1:
				Temp |= ADS1248_IDAC1_A1;
				break;
			case 2:
				Temp |= ADS1248_IDAC1_A2;
				break;
			case 3:
				Temp |= ADS1248_IDAC1_A3;
				break;
			case 4:
				Temp |= ADS1248_IDAC1_A4;
				break;
			case 5:
				Temp |= ADS1248_IDAC1_A5;
				break;
			case 6:
				Temp |= ADS1248_IDAC1_A6;
				break;
			case 7:
				Temp |= ADS1248_IDAC1_A7;
				break;
			case 8:
				Temp |= ADS1248_IDAC1_EXT1;
				break;
			case 9:
				Temp |= ADS1248_IDAC1_EXT2;
				break;
			case 10:
				Temp |= ADS1248_IDAC1_EXT1;
				break;
			case 11:
				Temp |= ADS1248_IDAC1_EXT2;
				break;
			case 12:
			case 13:
			case 14:
			case 15:
				Temp |= ADS1248_IDAC1_OFF;
				break;
			default:
				Temp |= ADS1248_IDAC1_OFF;
				dError = ADS1248_ERROR;
		}
	}
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_11_IDAC1, 0x01, &Temp);
	return dError;
}

// Relate to GPIOCFG
int ADS1248SetGPIOConfig(bDriverInterface_t *pdrv,unsigned char cdata)
{
	unsigned Temp;
	Temp = 0x00;
	if (cdata & 0x80)
		Temp |=  ADS1248_GPIO_7;
	if (cdata & 0x40)
		Temp |=  ADS1248_GPIO_6;
	if (cdata & 0x20)
		Temp |=  ADS1248_GPIO_5;
	if (cdata & 0x10)
		Temp |=  ADS1248_GPIO_4;
	if (cdata & 0x08)
		Temp |=  ADS1248_GPIO_3;
	if (cdata & 0x04)
		Temp |=  ADS1248_GPIO_2;
	if (cdata & 0x02)
		Temp |=  ADS1248_GPIO_1;
	if (cdata & 0x01)
		Temp |=  ADS1248_GPIO_0;
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_12_GPIOCFG, 0x01, &Temp);
	return ADS1248_NO_ERROR;
}

// Relate to GPIODIR
int ADS1248SetGPIODir(bDriverInterface_t *pdrv,unsigned char cdata)
{
	unsigned Temp;
	Temp = 0x00;
	if (cdata & 0x80)
		Temp |=  ADS1248_IO_7;
	if (cdata & 0x40)
		Temp |=  ADS1248_IO_6;
	if (cdata & 0x20)
		Temp |=  ADS1248_IO_5;
	if (cdata & 0x10)
		Temp |=  ADS1248_IO_4;
	if (cdata & 0x08)
		Temp |=  ADS1248_IO_3;
	if (cdata & 0x04)
		Temp |=  ADS1248_IO_2;
	if (cdata & 0x02)
		Temp |=  ADS1248_IO_1;
	if (cdata & 0x01)
		Temp |=  ADS1248_IO_0;
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_13_GPIODIR, 0x01, &Temp);
	return ADS1248_NO_ERROR;
}

// Relate to GPIODAT
int ADS1248SetGPIO(bDriverInterface_t *pdrv,unsigned char cdata)
{
	unsigned Temp;
	Temp = 0x00;
	if (cdata & 0x80)
		Temp |=  ADS1248_OUT_7;
	if (cdata & 0x40)
		Temp |=  ADS1248_OUT_6;
	if (cdata & 0x20)
		Temp |=  ADS1248_OUT_5;
	if (cdata & 0x10)
		Temp |=  ADS1248_OUT_4;
	if (cdata & 0x08)
		Temp |=  ADS1248_OUT_3;
	if (cdata & 0x04)
		Temp |=  ADS1248_OUT_2;
	if (cdata & 0x02)
		Temp |=  ADS1248_OUT_1;
	if (cdata & 0x01)
		Temp |=  ADS1248_OUT_0;
	// write the register value containing the new value back to the ADS
	ADS1248WriteRegister(pdrv,ADS1248_14_GPIODAT, 0x01, &Temp);
	return ADS1248_NO_ERROR;
}

/* Register Get Value Commands */
// Relate to MUX0
int ADS1248GetBurnOutSource(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_0_MUX0, 0x01, &Temp);
	return ((Temp >> 6) & 0x03);
}

int ADS1248GetChannel(bDriverInterface_t *pdrv,int cMux)			// cMux = 0, AINP; cMux = 1, AINN
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_0_MUX0, 0x01, &Temp);
	if (cMux==0)
		return ((Temp >> 3) & 0x07);
	else
		return (Temp  & 0x07);
}

// Relate to VBIAS
unsigned char ADS1248GetBias(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_1_VBIAS, 0x01, &Temp);
	return (Temp & 0xff);
}

//Relate to MUX1
int ADS1248GetCLKSTAT(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	return ((Temp >> 7) & 0x01);
}

int ADS1248GetIntRef(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	return ((Temp >> 5) & 0x03);
}

int ADS1248GetVoltageReference(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	return ((Temp >> 3) & 0x03);
}

int ADS1248GetSystemMonitor(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_2_MUX1, 0x01, &Temp);
	return (Temp & 0x07);
}

// Relate to SYS0
int ADS1248GetGain(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_3_SYS0, 0x01, &Temp);
	return ((Temp >> 4) & 0x07);
}

int ADS1248GetDataRate(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_3_SYS0, 0x01, &Temp);
	return (Temp & 0x0f);
}

// Relate to OFC (3 registers)
long ADS1248GetOFC(bDriverInterface_t *pdrv)
{
	long rData=0;
	unsigned rValue=0;
	unsigned regArray[3];
	int i;
	//write the desired default register settings for the first 4 registers NOTE: values shown are the POR values as per datasheet
	regArray[0] = 0x00;
	regArray[1] = 0x00;
	regArray[2] = 0x00;
	for (i=0; i<3; i++)
	{
		// read the register value for the OFC
		ADS1248ReadRegister(pdrv,(ADS1248_4_OFC0 + i), 0x01, &rValue);
		regArray[i] = rValue;
	}
	rData = regArray[2];
	rData = (rData<<8) | regArray[1];
	rData = (rData<<8) | regArray[0];
	return rData;
}

// Relate to FSC (3 registers)
long ADS1248GetFSC(bDriverInterface_t *pdrv)
{
	long rData=0;
	unsigned rValue=0;
	unsigned regArray[3];
	int i;
	//write the desired default register settings for the first 4 registers NOTE: values shown are the POR values as per datasheet
	regArray[0] = 0x00;
	regArray[1] = 0x00;
	regArray[2] = 0x00;
	for (i=0; i<3; i++)
	{
		// read the register value for the OFC
		ADS1248ReadRegister(pdrv,(ADS1248_7_FSC0 + i), 0x01, &rValue);
		regArray[i] = rValue;
	}
	rData = regArray[2];
	rData = (rData<<8) | regArray[1];
	rData = (rData<<8) | regArray[0];
	return rData;
}

// Relate to IDAC0
int ADS1248GetID(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_10_IDAC0, 0x01, &Temp);
	return ((Temp>>4) & 0x0f);
}

int ADS1248GetDRDYMode(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_10_IDAC0, 0x01, &Temp);
	return ((Temp>>3) & 0x01);
}

int ADS1248GetCurrentDACOutput(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_10_IDAC0, 0x01, &Temp);
	return (Temp & 0x07);
}

// Relate to IDAC1
int ADS1248GetIDACRouting(bDriverInterface_t *pdrv,int WhichOne) 		// IDACRoute (0 = I1DIR, 1 = I2DIR)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_11_IDAC1, 0x01, &Temp);
	if (WhichOne==0)
		return ((Temp>>4) & 0x0f);
	else
		return (Temp & 0x0f);
}

// Relate to GPIOCFG
unsigned char ADS1248GetGPIOConfig(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_12_GPIOCFG, 0x01, &Temp);
	return (Temp & 0xff);
}

// Relate to GPIODIR
unsigned char ADS1248GetGPIODir(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_13_GPIODIR, 0x01, &Temp);
	return (Temp & 0xff);
}

// Relate to GPIODAT
unsigned char ADS1248GetGPIO(bDriverInterface_t *pdrv)
{
	unsigned Temp;
	ADS1248ReadRegister(pdrv,ADS1248_14_GPIODAT, 0x01, &Temp);
	return (Temp & 0xff);
}

/* Miscellaneous Commands */
long ADS1248RDATACRead(bDriverInterface_t *pdrv)		// reads data directly based on RDATAC mode (writes NOP) and 32 SCLKs
{
	long Data;
	// assert CS to start transfer
	ADS1248AssertCS(pdrv,0);
	// get the conversion result
#ifdef ADS1148
	Data = ADS1248ReceiveByte(pdrv);
	Data = (Data << 8) | ADS1248ReceiveByte(pdrv);
    // sign extend data if the MSB is high (16 to 32 bit sign extension)
	if (Data & 0x8000)
		Data |= 0xffff0000;
#else
	Data = ADS1248ReceiveByte(pdrv);
	Data = (Data << 8) | ADS1248ReceiveByte(pdrv);
	Data = (Data << 8) | ADS1248ReceiveByte(pdrv);
	// sign extend data if the MSB is high (24 to 32 bit sign extension)
	if (Data & 0x800000)
		Data |= 0xff000000;
#endif
	// de-assert CS
	ADS1248AssertCS(pdrv,1);
	return Data;
}

/* Hardware Control Functions for Device Pin Control */
// Possible Need for Reset, Start (power down) (0-low, 1-high, 2-pulse)
int ADS1248SetStart(bDriverInterface_t *pdrv,int nStart)
{
	/*
	 * Code can be added here to set high or low the state of the pin for controlling the START pin
	 * which will differ depending on controller used and port pin assigned
	 */
	bDRIVER_GET_HALIF(_if, bADS124X_HalIf_t, pdrv);
	if (nStart)				// nStart=0 is START low, nStart=1 is START high
		bHalGpioWritePin(_if->start.port, _if->start.pin, 1);
	else
		bHalGpioWritePin(_if->start.port, _if->start.pin, 0);
	
	return ADS1248_NO_ERROR;
}

int ADS1248SetReset(bDriverInterface_t *pdrv,int nReset)
{
	/*
	 * Code can be added here to set high or low the state of the pin for controlling the RESET pin
	 * which will differ depending on controller used and port pin assigned
	 */
	bDRIVER_GET_HALIF(_if, bADS124X_HalIf_t, pdrv);	
	if (nReset)				// nReset=0 is RESET low, nReset=1 is RESET high
		bHalGpioWritePin(_if->reset.port, _if->reset.pin, 1);
	else
		bHalGpioWritePin(_if->reset.port, _if->reset.pin, 0);
	
	return ADS1248_NO_ERROR;
}


/*
 * ADS1248 Initial Configuration
 */
static int InitConfig(bDriverInterface_t *pdrv)
{
	int retval = -1;
	//establish some startup register settings
	unsigned regArray[4];
	// Send SDATAC command
	ADS1248SendSDATAC(pdrv);
	retval = ADS1248WaitForDataReady(pdrv,0);
	ADS1248SendSDATAC(pdrv);
	//write the desired default register settings for the first 4 registers NOTE: values shown are the POR values as per datasheet
	regArray[0] = 0x01;
	regArray[1] = 0x00;
	regArray[2] = 0x00;
	regArray[3] = 0x00;
	ADS1248WriteSequence(pdrv,ADS1248_0_MUX0, 4, regArray);
	return retval;
}

/**
 * \}
 */
static int _bAds124xRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
	uint32_t adc_raw_data = 0;
	adc_raw_data = ADS1248RDATACRead(pdrv);
	memcpy(pbuf, &adc_raw_data, sizeof(adc_raw_data));
	return sizeof(adc_raw_data);

}
static int _bAds124xCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
	switch (cmd)
	{
		case bCMD_ADS124X_SET_CH :
		break;
		case bCMD_ADS124X_SET_VREF :
		break;
		case bCMD_ADS124X_SET_GCONFIG :
		break;
		case bCMD_ADS124X_SET_GDAT :
		break;
		case bCMD_ADS124X_SET_FSC :
		break;
		case bCMD_ADS124X_SET_CURRENT :
		break;
		case bCMD_ADS124X_GET_GDAT :
		break;
		case bCMD_ADS124X_SET_OFC :
		break;
		case bCMD_ADS124X_SET_MUX :
		break;
		case bCMD_ADS124X_SET_BURNOUT :
		break;
		case bCMD_ADS124X_SET_BIAS :
		break;
		case bCMD_ADS124X_SET_PWRDN :
		break;
		case bCMD_ADS124X_SET_PGA :
		break;
		case bCMD_ADS124X_SET_IREF :
		break;
		case bCMD_ADS124X_RESETDUT :
		break;
		case bCMD_ADS124X_SET_DATARATE :
		break;
		case bCMD_ADS124X_SET_GIO :
		{
			uint8_t *cdata = (uint8_t *)param;
			ADS1248SetGPIODir(pdrv,*cdata);
		}
		break;
		case bCMD_ADS124X_SET_IDAC :
		{
			bADS124X_IDACRouting_t *IDACRouting = (bADS124X_IDACRouting_t *)param;
			ADS1248SetIDACRouting(pdrv,IDACRouting->IDACroute,IDACRouting->IDACdir);
		}
		break;
		default:
			break;
		
		
	}
	
	return 0;
}
/**
 * \addtogroup LIS3DH_Exported_Functions
 * \{
 */

int bADS124X_Init(bDriverInterface_t *pdrv)
{
	int retval = -1;
	bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bADS124X_Init);
	pdrv->read        = _bAds124xRead;
	pdrv->ctl         = _bAds124xCtl;

	retval = InitConfig(pdrv);

	return retval;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_ADS124X, bADS124X_Init);
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
