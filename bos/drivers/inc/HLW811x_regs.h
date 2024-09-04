
/**
 **********************************************************************************
 * @file   HLW811x_regs.h
 * @author Hossein.M (https://github.com/Hossein-M98)
 * @brief  HLW8110 and HLW8112 registers definitions
 **********************************************************************************
 *
 * Copyright (c) 2024 Mahda Embedded System (MIT License)
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
 *
 **********************************************************************************
 */

/* Define to prevent recursive inclusion ----------------------------------------*/
#ifndef	_HLW811X_REGS_H_
#define _HLW811X_REGS_H_

#ifdef __cplusplus
extern "C" {
#endif


/* Exported Constants -----------------------------------------------------------*/
/**
 * @brief  Register addresses
 */
#define HLW811X_REG_ADDR_SYSCON         0x00 // System Control Register
#define HLW811X_REG_ADDR_EMUCON         0x01 // Energy Measure Control Register
#define HLW811X_REG_ADDR_HFConst        0x02 // Pulse Frequency Register
#define HLW811X_REG_ADDR_Pastart        0x03 // Active Start Power Setting of Channel A
#define HLW811X_REG_ADDR_Pbstart        0x04 // Active Start Power Setting of Channel B
#define HLW811X_REG_ADDR_PAGain         0x05 // Channel A Power Gain Calibration Register
#define HLW811X_REG_ADDR_PBGain         0x06 // Channel B Power Gain Calibration Register
#define HLW811X_REG_ADDR_PhaseA         0x07 // Channel A Phase Calibration Register
#define HLW811X_REG_ADDR_PhaseB         0x08 // Channel B Phase Calibration Register
#define HLW811X_REG_ADDR_PAOS           0x0A // Channel A Active Power Offset Calibration
#define HLW811X_REG_ADDR_PBOS           0x0B // Channel B Active Power Offset Calibration
#define HLW811X_REG_ADDR_RmsIAOS        0x0E // Current Channel A RMS Offset Compensation
#define HLW811X_REG_ADDR_RmsIBOS        0x0F // Current Channel B RMS Offset Compensation
#define HLW811X_REG_ADDR_IBGain         0x10 // Current Channel B Gain Settings
#define HLW811X_REG_ADDR_PSGain         0x11 // Apparent power gain calibration
#define HLW811X_REG_ADDR_PSOS           0x12 // Visual Power Offset Compensation
#define HLW811X_REG_ADDR_EMUCON2        0x13 // Meter Control Register 2
#define HLW811X_REG_ADDR_DCIA           0x14 // IA Channel DC offset Correction Register
#define HLW811X_REG_ADDR_DCIB           0x15 // IB Channel DC offset Correction Register
#define HLW811X_REG_ADDR_DCIC           0x16 // U Channel DC offset Correction Register
#define HLW811X_REG_ADDR_SAGCYC         0x17 // Voltage sag period setting
#define HLW811X_REG_ADDR_SAGLVL         0x18 // Voltage sag threshold setting
#define HLW811X_REG_ADDR_OVLVL          0x19 // Voltage Overvoltage Threshold Setting
#define HLW811X_REG_ADDR_OIALVL         0x1A // Current Channel A Overcurrent Threshold Setting
#define HLW811X_REG_ADDR_OIBLVL         0x1B // Current Channel B Overcurrent Threshold Setting
#define HLW811X_REG_ADDR_OPLVL          0x1C // Threshold setting of active power overload
#define HLW811X_REG_ADDR_INT            0x1D // INT1/INT2 interrupt set defaults to output PFA defaults to output PFB
#define HLW811X_REG_ADDR_PFCntPA        0x20 // Fast Combination Active Pulse Counting of Channel A
#define HLW811X_REG_ADDR_PFCntPB        0x21 // Fast Combination Active Pulse Counting of Channel B
#define HLW811X_REG_ADDR_Angle          0x22 // The angle between current and voltage is selected by command: Current Channel A/B Phase Angle with Voltage Channel
#define HLW811X_REG_ADDR_Ufreq          0x23 // Voltage Frequency (L Line)
#define HLW811X_REG_ADDR_RmsIA          0x24 // IARms
#define HLW811X_REG_ADDR_RmsIB          0x25 // IBRms
#define HLW811X_REG_ADDR_RmsU           0x26 // URms
#define HLW811X_REG_ADDR_PowerFactor    0x27 // Power Factor Register, Selected by Command: Channel A/B Power Factor
#define HLW811X_REG_ADDR_Energy_PA      0x28 // Channel A active power, default to zero after reading, can be configured to zero after reading
#define HLW811X_REG_ADDR_Energy_PB      0x29 // Channel B active power, default to zero after reading, can be configured to zero after reading
#define HLW811X_REG_ADDR_PowerPA        0x2C // Active power of channel A, update rate 3.4 Hz, 6.8 Hz, 13.6 Hz, 27.2 Hz
#define HLW811X_REG_ADDR_PowerPB        0x2D // Active power of channel B, update rate 3.4 Hz, 6.8 Hz, 13.6 Hz, 27.2 Hz
#define HLW811X_REG_ADDR_PowerS         0x2E // The apparent power of channel A/B is selected by command. Updating rates of 3.4 Hz, 6.8 Hz, 13.6 Hz and 27.2 Hz
#define HLW811X_REG_ADDR_EMUStatus      0x2F // Measurement Status and Check and Register
#define HLW811X_REG_ADDR_PeakIA         0x30 // Peak of Current Channel A
#define HLW811X_REG_ADDR_PeakIB         0x31 // Peak of Current Channel B
#define HLW811X_REG_ADDR_PeakU          0x32 // Peak Value of Voltage Channel U
#define HLW811X_REG_ADDR_InstanIA       0x33 // Current Channel A Instantaneous Value
#define HLW811X_REG_ADDR_InstanIB       0x34 // Current Channel B Instantaneous Value
#define HLW811X_REG_ADDR_InstanU        0x35 // Instantaneous Value of Voltage Channel
#define HLW811X_REG_ADDR_WaveIA         0x36 // Current Channel A Waveform
#define HLW811X_REG_ADDR_WaveIB         0x37 // Current Channel B Waveform
#define HLW811X_REG_ADDR_WaveU          0x38 // Voltage Channel Waveform
#define HLW811X_REG_ADDR_InstanP        0x3C // Active power instantaneous value, select channel A by Command Or the instantaneous value of active power in channel B
#define HLW811X_REG_ADDR_InstanS        0x3D // Depending on the instantaneous power value, channel A is selected by command. Or the instantaneous real power of channel B
#define HLW811X_REG_ADDR_IE             0x40 // Interrupt admission register
#define HLW811X_REG_ADDR_IF             0x41 // Interrupt flag register (not writable)
#define HLW811X_REG_ADDR_RIF            0x42 // Reset the interrupt status register and clear it after reading
#define HLW811X_REG_ADDR_SysStatus      0x43 // System Status Register
#define HLW811X_REG_ADDR_Rdata          0x44 // Data read by SPI last time
#define HLW811X_REG_ADDR_Wdata          0x45 // Data written by the last SPI
#define HLW811X_REG_ADDR_Coeff_chksum   0x6F // Coefficient checksum
#define HLW811X_REG_ADDR_RmsIAC         0x70 // Current Channel A RMS Conversion Coefficient
#define HLW811X_REG_ADDR_RmsIBC         0x71 // Current Channel B RMS Conversion Coefficient
#define HLW811X_REG_ADDR_RmsUC          0x72 // U-RMS Conversion Coefficient of Voltage Channel
#define HLW811X_REG_ADDR_PowerPAC       0x73 // Active Power Conversion Coefficient of Current Channel A
#define HLW811X_REG_ADDR_PowerPBC       0x74 // Active Power Conversion Coefficient of Current Channel B
#define HLW811X_REG_ADDR_PowerSC        0x75 // Apparent power conversion coefficient
#define HLW811X_REG_ADDR_EnergyAC       0x76 // Energy Conversion Coefficient of A Channel
#define HLW811X_REG_ADDR_EnergyBC       0x77 // Energy Conversion Coefficient of B Channel
#define HLW811X_REG_ADDR_Command        0xEA // Command Register

/**
 * @brief  Register bits of SYSCON register
 */
#define HLW811X_REG_SYSCON_ADC3ON         11
#define HLW811X_REG_SYSCON_ADC2ON         10
#define HLW811X_REG_SYSCON_ADC1ON         9
#define HLW811X_REG_SYSCON_PGAIB          6
#define HLW811X_REG_SYSCON_PGAU           3
#define HLW811X_REG_SYSCON_PGAIA          0

/**
 * @brief  Register bits of EMUCON register
 */
#define HLW811X_REG_EMUCON_Tsensor_Step   14
#define HLW811X_REG_EMUCON_tensor_en      13
#define HLW811X_REG_EMUCON_comp_off       12
#define HLW811X_REG_EMUCON_Pmode          10
#define HLW811X_REG_EMUCON_DC_MODE        9
#define HLW811X_REG_EMUCON_ZXD1           8
#define HLW811X_REG_EMUCON_ZXD0           7
#define HLW811X_REG_EMUCON_HPFIBOFF       6
#define HLW811X_REG_EMUCON_HPFIAOFF       5
#define HLW811X_REG_EMUCON_HPFUOFF        4
#define HLW811X_REG_EMUCON_PBRUN          1
#define HLW811X_REG_EMUCON_PARUN          0

/**
 * @brief  Register bits of EMUCON2 register
 */
#define HLW811X_REG_EMUCON2_SDOCmos       12
#define HLW811X_REG_EMUCON2_EPB_CB        11
#define HLW811X_REG_EMUCON2_EPB_CA        10
#define HLW811X_REG_EMUCON2_DUPSEL        8
#define HLW811X_REG_EMUCON2_CHS_IB        7
#define HLW811X_REG_EMUCON2_PfactorEN     6
#define HLW811X_REG_EMUCON2_WaveEN        5
#define HLW811X_REG_EMUCON2_SAGEN         4
#define HLW811X_REG_EMUCON2_OverEN        3
#define HLW811X_REG_EMUCON2_ZxEN          2
#define HLW811X_REG_EMUCON2_PeakEN        1
#define HLW811X_REG_EMUCON2_VrefSel       0

/**
 * @brief  Register bits of INT register
 */
#define HLW811X_REG_INT_P2sel             4
#define HLW811X_REG_INT_P1sel             0

/**
 * @brief  Register bits of EMUStatus register
 */
#define HLW811X_REG_EMUStatus_Channel_sel   21
#define HLW811X_REG_EMUStatus_NopldB        20
#define HLW811X_REG_EMUStatus_NopldA        19
#define HLW811X_REG_EMUStatus_REVPB         18
#define HLW811X_REG_EMUStatus_REVPA         17
#define HLW811X_REG_EMUStatus_ChksumBusy    16
#define HLW811X_REG_EMUStatus_Chksum        0

/**
 * @brief  Register bits of IE register
 */
#define HLW811X_REG_IE_LeakageIE            15
#define HLW811X_REG_IE_ZX_UIE               14
#define HLW811X_REG_IE_ZX_IBIE              13
#define HLW811X_REG_IE_ZX_IAIE              12
#define HLW811X_REG_IE_SAGIE                11
#define HLW811X_REG_IE_OPIE                 10
#define HLW811X_REG_IE_OVIE                 9
#define HLW811X_REG_IE_OIBIE                8
#define HLW811X_REG_IE_OIAIE                7
#define HLW811X_REG_IE_INSTANIE             6
#define HLW811X_REG_IE_PEBOIE               4
#define HLW811X_REG_IE_PEAOIE               3
#define HLW811X_REG_IE_PFBIE                2
#define HLW811X_REG_IE_PFAIE                1
#define HLW811X_REG_IE_DUPDIE               0

/**
 * @brief  Register bits of IF register
 */
#define HLW811X_REG_IF_LeakageIF            15
#define HLW811X_REG_IF_ZX_UIF               14
#define HLW811X_REG_IF_ZX_IBIF              13
#define HLW811X_REG_IF_ZX_IAIF              12
#define HLW811X_REG_IF_SAGIF                11
#define HLW811X_REG_IF_OPIF                 10
#define HLW811X_REG_IF_OVIF                 9
#define HLW811X_REG_IF_OIBIF                8
#define HLW811X_REG_IF_OIAIF                7
#define HLW811X_REG_IF_INSTANIF             6
#define HLW811X_REG_IF_PEBOIF               4
#define HLW811X_REG_IF_PEAOIF               3
#define HLW811X_REG_IF_PFBIF                2
#define HLW811X_REG_IF_PFAIF                1
#define HLW811X_REG_IF_DUPDIF               0

/**
 * @brief  Register bits of RIF register
 */
#define HLW811X_REG_RIF_RleakageIF          15
#define HLW811X_REG_RIF_RZX_UIF             14
#define HLW811X_REG_RIF_RZX_IBIF            13
#define HLW811X_REG_RIF_RZX_IAIF            12
#define HLW811X_REG_RIF_RSAGIF              11
#define HLW811X_REG_RIF_ROPIF               10
#define HLW811X_REG_RIF_ROVIF               9
#define HLW811X_REG_RIF_ROIBIF              8
#define HLW811X_REG_RIF_ROIAIF              7
#define HLW811X_REG_RIF_RINSTANIF           6
#define HLW811X_REG_RIF_RPEBOIF             4
#define HLW811X_REG_RIF_RPEAOIF             3
#define HLW811X_REG_RIF_RPFBIF              2
#define HLW811X_REG_RIF_RPFAIF              1
#define HLW811X_REG_RIF_RDUPDIF             0

/**
 * @brief  Register bits of SysStatus register
 */
#define HLW811X_REG_SysStatus_clksel        6
#define HLW811X_REG_SysStatus_WREN          5
#define HLW811X_REG_SysStatus_RST           0



#ifdef __cplusplus
}
#endif

#endif //! _HLW811X_REGS_H_
















