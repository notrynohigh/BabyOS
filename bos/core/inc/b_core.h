/**
 *!
 * \file        b_core.h
 * \version     v0.0.1
 * \date        2019/06/05
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
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
#ifndef __B_CORE_H__
#define __B_CORE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup CORE
 * \{
 */

/**
 * \addtogroup CORE
 * \{
 */

/**
 * \defgroup CORE_Exported_TypesDefinitions
 * \{
 */
typedef struct
{
    uint8_t  flag;
    uint8_t  status;
    uint32_t number;
    uint32_t lseek;
} bCoreFd_t;

/**
 * \}
 */

/**
 * \defgroup CORE_Exported_Defines
 * \{
 */

#define BCORE_FLAG_R 0
#define BCORE_FLAG_W 1
#define BCORE_FLAG_RW 2

#define BCORE_STA_NULL 0
#define BCORE_STA_OPEN 1
/**
 * \}
 */

/**
 * \defgroup CORE_Exported_Macros
 * \{
 */
#define IS_VALID_FLAG(n) (n == BCORE_FLAG_R || n == BCORE_FLAG_W || n == BCORE_FLAG_RW)
#define READ_IS_VALID(n) (n == BCORE_FLAG_R || n == BCORE_FLAG_RW)
#define WRITE_IS_VALID(n) (n == BCORE_FLAG_W || n == BCORE_FLAG_RW)
/**
 * \}
 */

/**
 * \defgroup CORE_Exported_Functions
 * \{
 */

int bOpen(uint32_t dev_no, uint8_t flag);
int bRead(int fd, uint8_t *pdata, uint32_t len);
int bWrite(int fd, uint8_t *pdata, uint32_t len);
int bCtl(int fd, uint8_t cmd, void *param);
int bLseek(int fd, uint32_t off);
int bClose(int fd);

int bInit(void);
int bExec(void);
int bReinit(uint32_t dev_no);

int bModifyHalIf(uint32_t dev_no, uint32_t type_size, uint32_t off, const uint8_t *pval,
                 uint8_t len);

uint8_t bFdIsReadable(int fd);
uint8_t bFdIsWritable(int fd);
uint8_t bFdIsAbnormal(int fd);

int bGetDevNumber(int fd, uint32_t *pdev_no);
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

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
