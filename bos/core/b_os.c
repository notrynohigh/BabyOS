/**
 *!
 * \file        b_os.c
 * \version     v0.0.1
 * \date        2020/02/11
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
   
/*Includes ----------------------------------------------*/
#include "b_os.h"

/** 
 * \addtogroup BABYOS
 * \{
 */
/** 
 * \addtogroup CORE
 * \{
 */
 
/** 
 * \addtogroup BOS
 * \{
 */

/** 
 * \defgroup BOS_Private_TypesDefinitions
 * \{
 */
   
/**
 * \}
 */


/** 
 * \defgroup BOS_Private_Defines
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup BOS_Private_Macros
 * \{
 */

/**
 * \}
 */
   
/** 
 * \defgroup BOS_Private_Variables
 * \{
 */


/**
 * \}
 */
   
/** 
 * \defgroup BOS_Private_FunctionPrototypes
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup BOS_Private_Functions
 * \{
 */


/**
 * \}
 */
   
/** 
 * \addtogroup BOS_Exported_Functions
 * \{
 */

/**
 * \brief Init 
 * \retval Result
 *          \arg 0  OK
 *          \arg -1 ERR
 */
int bInit()
{
    b_log("______________________________________________\n\r");
    b_log("    ____                         __       __  \n\r");
    b_log("    /   )          /           /    )   /    \\\n\r");
    b_log("---/__ /-----__---/__---------/----/----\\-----\n\r");
    b_log("  /    )   /   ) /   ) /   / /    /      \\    \n\r");
    b_log("_/____/___(___(_(___/_(___/_(____/___(____/___\n\r");
    b_log("                         /                    \n\r");
    b_log("                     (_ /                     \n\r");
    b_log("HW:%d.%d.%d FW:%d.%d.%d COMPILE:%s-%s\r\n", (HW_VERSION / 10000), (HW_VERSION % 10000) / 100,
            HW_VERSION % 100, (FW_VERSION / 10000), (FW_VERSION % 10000) / 100,
            FW_VERSION % 100,__DATE__, __TIME__);
    bHalInit();
    b_log("device number:%d\r\n", bDEV_MAX_NUM);
    return bDeviceInit();
}

/**
 * \brief  Call this function inside the while(1)
 * \retval Result
 *          \arg 0  OK
 *          \arg -1 ERR
 */
int bExec()
{
    bHalUartDetectIdle();
#if _BATTERY_ENABLE
    BOS_PERIODIC_TASK(bBatteryCore, _BATTERY_D_CYCLE);
#endif  

#if _ERROR_MANAGE_ENABLE
    BOS_PERIODIC_TASK(bErrorCore, 1000);
#endif

#if _XMODEM128_ENABLE
    bXmodem128Timeout();
#endif

#if _YMODEM_ENABLE
    bYmodemTimeout();
#endif

#if _FLEXIBLEBUTTON_ENABLE
    BOS_PERIODIC_TASK(flex_button_scan, 20);
#endif

#if _EVENT_MANAGE_ENABLE
    bEventCore();
#endif    
     
#if _UGUI_ENABLE
    BOS_PERIODIC_TASK(bGUI_TouchExec, 10);
    UG_Update();
#endif   


/*************************************************b_utils****/
    bAsyntxCore();
    
/**************************************************b_hal*****/
    bHalCore();

    return 0;
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
  
/************************ Copyright (c) 2019 Bean *****END OF FILE****/




