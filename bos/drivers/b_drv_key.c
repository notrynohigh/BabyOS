/**
 *!
 * \file        b_drv_key.c
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_key.h"
#include "b_os.h"
/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup KEY
 * \{
 */

/**
 * \defgroup KEY_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup KEY_Private_Defines
 * \{
 */
#define DRIVER_NAME KEY
/**
 * \}
 */

/**
 * \defgroup KEY_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup KEY_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bKEY_HalIf_t, DRIVER_NAME);
/**
 * \}
 */

/**
 * \defgroup KEY_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup KEY_Private_Functions
 * \{
 */

static int _bKeyRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
	int fd = -1;	
	if (pbuf == NULL || len == 0)
    {
        return -1;
    }
    bDRIVER_GET_HALIF(_if, bKEY_HalIf_t, pdrv);

	if (_if->port == 0xFF)
    {
        uint32_t tm1638_keys = 0;
        pbuf[0] = 0; 
        int fd = bOpen(bTM1638, BCORE_FLAG_RW); 
        if (fd >= 0)
        {
            bRead(fd, (uint8_t *)&tm1638_keys, sizeof(tm1638_keys));
            bClose(fd);
        }
        if (tm1638_keys & (1 << _if->pin))
        {
            pbuf[0] = 1; // 1 代表按下
        }
    }
    else
    {
	    pbuf[0] = (bHalGpioReadPin(_if->port, _if->pin) == _if->level);
    }	

    return 1;
}

/**
 * \}
 */

/**
 * \addtogroup KEY_Exported_Functions
 * \{
 */
int bKEY_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bKEY_Init);
    pdrv->read = _bKeyRead;
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_KEY, bKEY_Init);
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
