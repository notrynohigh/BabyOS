/**
 *!
 * \file        b_srv_transfile.h
 * \version     v0.0.1
 * \date        2023/08/27
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 Bean
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
#ifndef __B_SRV_TRANSFILE_H__
#define __B_SRV_TRANSFILE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_TRANSFILE_SERVICE_ENABLE) && (_TRANSFILE_SERVICE_ENABLE == 1))

#include "services/inc/b_srv_protocol.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup TRANSFILE
 * \{
 */

/**
 * \defgroup TRANSFILE_Exported_TypesDefinitions
 * \{
 */

typedef void (*bTFLSrvSendData_t)(uint8_t *pbuf, uint16_t len);

typedef struct
{
    uint32_t addr;
    uint32_t len;
} bTFLSrvFileInfo_t;

/**
 * \}
 */

/**
 * \defgroup TRANSFILE_Exported_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TRANSFILE_Exported_Functions
 * \{
 */

int bTFLSrvInit(bProtSrvId_t protocol_id, bTFLSrvSendData_t send);
int bTFLSrvDeinit(void);

int bTFLSrvGetFileInfo(uint32_t dev_no, uint32_t base_addr, const char *filename,
                       bTFLSrvFileInfo_t *pinfo);

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

/************************ Copyright (c) 2023 Bean *****END OF FILE****/
