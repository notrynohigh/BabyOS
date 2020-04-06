/**
 *!
 * \file        b_hal_i2c.c
 * \version     v0.0.1
 * \date        2020/03/25
 * \author      Bean(notrynohigh@outlook.com)
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SI2CL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
   
/*Includes ----------------------------------------------*/
#include "b_hal.h" 
#include "b_utils.h"
/** 
 * \addtogroup B_HAL
 * \{
 */

/** 
 * \addtogroup I2C
 * \{
 */

/** 
 * \defgroup I2C_Private_TypesDefinitions
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup I2C_Private_Defines
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup I2C_Private_Macros
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup I2C_Private_Variables
 * \{
 */
static bUtilI2C_t I2C_IO_3 = {
    .sda = {B_HAL_GPIOB, B_HAL_PIN7},
    .clk = {B_HAL_GPIOB, B_HAL_PIN6}
};
/**
 * \}
 */
   
/** 
 * \defgroup I2C_Private_FunctionPrototypes
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup I2C_Private_Functions
 * \{
 */


/**
 * \}
 */
   
/** 
 * \addtogroup I2C_Exported_Functions
 * \{
 */

void bHalI2C_SendByte(uint8_t no, uint8_t dev_addr, uint8_t dat)
{
    switch(no)
    {
        case B_HAL_I2C_1:
            
            break;        
        case B_HAL_I2C_2:
            
            break;

        default:
            break;
    }
}

uint8_t bHalI2C_ReceiveByte(uint8_t no, uint8_t dev_addr)
{
    uint8_t tmp;
    switch(no)
    {
        case B_HAL_I2C_1:
            
            break;        
        case B_HAL_I2C_2:
            
            break;
        default:
            break;
    }
    return tmp;
}


int bHalI2C_MemWrite(uint8_t no, uint8_t dev_addr, uint16_t mem_addr, uint8_t *pbuf, uint16_t len)
{
    int retval = 0;
    int i = 0;
    switch(no)
    {
        case B_HAL_I2C_1:

            break;        
        case B_HAL_I2C_2:
            
            break;
        case B_HAL_I2C_3:
            for(i = 0;i < len;i++)
            {
                bUtilI2C_WriteData(I2C_IO_3, dev_addr, mem_addr + i, pbuf[i]);
                bUtilDelayMS(5);
            }
            break;        
        default:
            break;
    }
    return retval;
}


int bHalI2C_MemRead(uint8_t no, uint8_t dev_addr, uint16_t mem_addr, uint8_t *pbuf, uint16_t len)
{
    int retval = 0;
    int i = 0;
    switch(no)
    {
        case B_HAL_I2C_1:
 
            break;        
        case B_HAL_I2C_2:
            
            break;
        case B_HAL_I2C_3:
            for(i = 0;i < len;i++)
            {
                pbuf[i] = bUtilI2C_ReadData(I2C_IO_3, dev_addr, mem_addr + i);
            }
            break;        
        default:
            break;
    }
    return retval;
}

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


