

/* Define to prevent recursive inclusion ----------------------------------------*/
#ifndef	_ADS124X_REGS_H_
#define _ADS124X_REGS_H_

#ifdef __cplusplus
extern "C" {
#endif


/* Exported Constants -----------------------------------------------------------*/
/**
 * @brief  Register addresses
 */
 

/* Command Definitions */
// System Control
#define ADS1248_CMD_WAKEUP    	0x00
#define ADS1248_CMD_SLEEP     	0x03
#define ADS1248_CMD_SYNC     	0x05
#define ADS1248_CMD_RESET    	0x07
#define ADS1248_CMD_NOP    		0xFF
// Data Read
#define ADS1248_CMD_RDATA    	0x13
#define ADS1248_CMD_RDATAC    	0x15
#define ADS1248_CMD_SDATAC    	0x17
// Read Register
#define ADS1248_CMD_RREG    	0x20
// Write Register
#define ADS1248_CMD_WREG    	0x40
// Calibration
#define ADS1248_CMD_SYSOCAL    	0x60
#define ADS1248_CMD_SYSGCAL    	0x61
#define ADS1248_CMD_SELFOCAL    0x62


/* ADS1248 Register Definitions */
#define ADS1248_0_MUX0   		0x00
#define ADS1248_1_VBIAS     	0x01
#define ADS1248_2_MUX1	     	0x02
#define ADS1248_3_SYS0	    	0x03
#define ADS1248_4_OFC0	    	0x04
#define ADS1248_5_OFC1	    	0x05
#define ADS1248_6_OFC2	    	0x06
#define ADS1248_7_FSC0	    	0x07
#define ADS1248_8_FSC1	    	0x08
#define ADS1248_9_FSC2	    	0x09
#define ADS1248_10_IDAC0	   	0x0A
#define ADS1248_11_IDAC1	   	0x0B
#define ADS1248_12_GPIOCFG    	0x0C
#define ADS1248_13_GPIODIR    	0x0D
#define ADS1248_14_GPIODAT    	0x0E

/* ADS1248 Register 0 (MUX0) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//        BCS[1:0]       |             MUX_SP[2:0]           |            MUX_SN[2:0]
//
// Define BCS (burnout current source)
#define ADS1248_BCS_OFF			0x00
#define ADS1248_BCS_500nA		0x40
#define ADS1248_BCS_2uA			0x80
#define ADS1248_BCS_10uA		0xC0
// Define Positive MUX Input Channels
#define ADS1248_AINP0			0x00
#define ADS1248_AINP1			0x08
#define ADS1248_AINP2			0x10
#define ADS1248_AINP3			0x18
#define ADS1248_AINP4			0x20
#define ADS1248_AINP5			0x28
#define ADS1248_AINP6			0x30
#define ADS1248_AINP7			0x38
// Define Negative Mux Input Channels
#define ADS1248_AINN0			0x00
#define ADS1248_AINN1			0x01
#define ADS1248_AINN2			0x02
#define ADS1248_AINN3			0x03
#define ADS1248_AINN4			0x04
#define ADS1248_AINN5			0x05
#define ADS1248_AINN6			0x06
#define ADS1248_AINN7			0x07

/* ADS1248 Register 1 (VBIAS) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         VBIAS[7:0]
//
#define ADS1248_VBIAS_OFF		0x00
#define ADS1248_VBIAS0			0x01
#define ADS1248_VBIAS1			0x02
#define ADS1248_VBIAS2			0x04
#define ADS1248_VBIAS3			0x08
#define ADS1248_VBIAS4			0x10
#define ADS1248_VBIAS5			0x20
#define ADS1248_VBIAS6			0x40
#define ADS1248_VBIAS7			0x80


/* ADS1248 Register 2 (MUX1) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//  CLKSTAT  |       VREFCON[1:0]    |      REFSELT[1:0]     |            MUXCAL[2:0]
//
// Define Internal Reference
#define ADS1248_INT_VREF_OFF	0x00
#define ADS1248_INT_VREF_ON		0x20
#define ADS1248_INT_VREF_CONV	0x40
// Define Reference Select
#define ADS1248_REF0			0x00
#define ADS1248_REF1			0x08
#define ADS1248_INT				0x10
#define ADS1248_INT_REF0		0x18
// Define System Monitor
#define ADS1248_MEAS_NORM		0x00
#define ADS1248_MEAS_OFFSET		0x01
#define ADS1248_MEAS_GAIN		0x02
#define ADS1248_MEAS_TEMP		0x03
#define ADS1248_MEAS_REF1		0x04
#define ADS1248_MEAS_REF0		0x05
#define ADS1248_MEAS_AVDD		0x06
#define ADS1248_MEAS_DVDD		0x07

/* ADS1248 Register 3 (SYS0) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//     0     |              PGA[2:0]             |                   DOR[3:0]
//
// Define Gain
#define ADS1248_GAIN_1			0x00
#define ADS1248_GAIN_2			0x10
#define ADS1248_GAIN_4			0x20
#define ADS1248_GAIN_8			0x30
#define ADS1248_GAIN_16			0x40
#define ADS1248_GAIN_32			0x50
#define ADS1248_GAIN_64			0x60
#define ADS1248_GAIN_128		0x70
//Define data rate
#define ADS1248_DR_5			0x00
#define ADS1248_DR_10			0x01
#define ADS1248_DR_20			0x02
#define ADS1248_DR_40			0x03
#define ADS1248_DR_80			0x04
#define ADS1248_DR_160			0x05
#define ADS1248_DR_320			0x06
#define ADS1248_DR_640			0x07
#define ADS1248_DR_1000			0x08
#define ADS1248_DR_2000			0x09

/* ADS1248 Register 4 (OFC0) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         OFC0[7:0]
//

/* ADS1248 Register 5 (OFC1) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         OFC1[7:0]
//

/* ADS1248 Register 6 (OFC2) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         OFC2[7:0]
//

/* ADS1248 Register 7 (FSC0) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         FSC0[7:0]
//

/* ADS1248 Register 8 (FSC1) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         FSC1[7:0]
//

/* ADS1248 Register 9 (FSC2) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         FSC2[7:0]
//

/* ADS1248 Register A (IDAC0) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                     ID[3:0]                   | DRDY_MODE |              IMAG[2:0]
//
// Define DRDY mode on DOUT
#define ADS1248_DRDY_OFF		0x00
#define ADS1248_DRDY_ON			0x08
//Define IDAC Magnitude
#define ADS1248_IDAC_OFF		0x00
#define ADS1248_IDAC_50			0x01
#define ADS1248_IDAC_100		0x02
#define ADS1248_IDAC_250		0x03
#define ADS1248_IDAC_500		0x04
#define ADS1248_IDAC_750		0x05
#define ADS1248_IDAC_1000		0x06
#define ADS1248_IDAC_1500		0x07

/* ADS1248 Register B (IDAC1) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                   I1DIR[3:0]                  |                   I2DIR[3:0]
//
// Define IDAC1 Output
#define ADS1248_IDAC1_A0		0x00
#define ADS1248_IDAC1_A1		0x10
#define ADS1248_IDAC1_A2		0x20
#define ADS1248_IDAC1_A3		0x30
#define ADS1248_IDAC1_A4		0x40
#define ADS1248_IDAC1_A5		0x50
#define ADS1248_IDAC1_A6		0x60
#define ADS1248_IDAC1_A7		0x70
#define ADS1248_IDAC1_EXT1		0x80
#define ADS1248_IDAC1_EXT2		0x90
#define ADS1248_IDAC1_OFF		0xF0
// Define IDAC2 Output
#define ADS1248_IDAC2_A0		0x00
#define ADS1248_IDAC2_A1		0x01
#define ADS1248_IDAC2_A2		0x02
#define ADS1248_IDAC2_A3		0x03
#define ADS1248_IDAC2_A4		0x04
#define ADS1248_IDAC2_A5		0x05
#define ADS1248_IDAC2_A6		0x06
#define ADS1248_IDAC2_A7		0x07
#define ADS1248_IDAC2_EXT1		0x08
#define ADS1248_IDAC2_EXT2		0x09
#define ADS1248_IDAC2_OFF		0x0F

/* ADS1248 Register C (GPIOCFG) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         IOCFG[7:0]
//
// Define GPIO (0-Analog; 1-GPIO)
#define ADS1248_GPIO_0			0x01
#define ADS1248_GPIO_1			0x02
#define ADS1248_GPIO_2			0x04
#define ADS1248_GPIO_3			0x08
#define ADS1248_GPIO_4			0x10
#define ADS1248_GPIO_5			0x20
#define ADS1248_GPIO_6			0x40
#define ADS1248_GPIO_7			0x80

/* ADS1248 Register D (GPIODIR) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         IODIR[7:0]
//
// Define GPIO Direction (0-Output; 1-Input)
#define ADS1248_IO_0			0x01
#define ADS1248_IO_1			0x02
#define ADS1248_IO_2			0x04
#define ADS1248_IO_3			0x08
#define ADS1248_IO_4			0x10
#define ADS1248_IO_5			0x20
#define ADS1248_IO_6			0x40
#define ADS1248_IO_7			0x80

/* ADS1248 Register E (GPIODAT) Definition */
//   Bit 7   |   Bit 6   |   Bit 5   |   Bit 4   |   Bit 3   |   Bit 2   |   Bit 1   |   Bit 0
//--------------------------------------------------------------------------------------------
//                                         IOIDAT[7:0]
//
#define ADS1248_OUT_0			0x01
#define ADS1248_OUT_1			0x02
#define ADS1248_OUT_2			0x04
#define ADS1248_OUT_3			0x08
#define ADS1248_OUT_4			0x10
#define ADS1248_OUT_5			0x20
#define ADS1248_OUT_6			0x40
#define ADS1248_OUT_7			0x80



#ifdef __cplusplus
}
#endif

#endif //! _ADS124X_REGS_H_
















