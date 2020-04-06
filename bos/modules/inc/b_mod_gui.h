/**
 *!
 * \file        b_mod_gui.h
 * \version     v0.0.1
 * \date        2020/03/02
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO GUI SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_MOD_GUI_H__
#define __B_MOD_GUI_H__

#ifdef __cplusplus
 extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "b_config.h"
#if _UGUI_ENABLE
#include "UGUI.h"
/** 
 * \addtogroup BABYOS
 * \{
 */

/** 
 * \addtogroup MODULES
 * \{
 */

/** 
 * \addtogroup GUI
 * \{
 */

/** 
 * \defgroup GUI_Exported_TypesDefinitions
 * \{
 */
typedef struct
{
    int lcd_id;
    int touch_id;
}bGUI_Info_t;

/**
 * \}
 */
   
/** 
 * \defgroup GUI_Exported_Defines
 * \{
 */


/**
 * \}
 */
   
/** 
 * \defgroup GUI_Exported_Macros
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup GUI_Exported_Variables
 * \{
 */
   
/**
 * \}
 */
   
/** 
 * \defgroup GUI_Exported_Functions
 * \{
 */
int bGUI_Init(int lcd, int touch);


///<Called in bExec()
void bGUI_TouchExec(void);

/**
 * \}
 */
 
 
/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */
#endif

#ifdef __cplusplus
	}
#endif

#endif  

/************************ Copyright (c) 2019 Bean *****END OF FILE****/


