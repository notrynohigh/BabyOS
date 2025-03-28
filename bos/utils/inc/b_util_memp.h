/**
 *!
 * \file        b_util_memp.h
 * \version     v0.0.1
 * \date        2019/12/23
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
#ifndef __B_UTIL_MEMP_H__
#define __B_UTIL_MEMP_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdlib.h>
#include <string.h>

#include "b_config.h"

/**
 * \addtogroup B_UTILS
 * \{
 */

/**
 * \addtogroup MEMP
 * \{
 */

#if (defined(_MEMP_ENABLE) && (_MEMP_ENABLE == 1))
/**
 * \defgroup MEMP_Exported_Functions
 * \{
 */
#if (defined(_MEMP_MONITOR_ENABLE) && (_MEMP_MONITOR_ENABLE == 1))

void* bMallocPlus(uint32_t size, const char* func, int line);
void* bCallocPlus(uint32_t num, uint32_t size, const char* func, int line);
void  bFreePlus(void* ptr, const char* func, int line);
void* bReallocPlus(void* ptr, uint32_t size, const char* func, int line);

#define bMalloc(size) bMallocPlus((size), __func__, __LINE__)
#define bCalloc(num, size) bCallocPlus((num), (size), __func__, __LINE__)
#define bFree(addr) bFreePlus((addr), __func__, __LINE__)
#define bRealloc(addr, size) bReallocPlus((addr), (size), __func__, __LINE__)
#else

void *bMalloc(uint32_t size);
void *bCalloc(uint32_t num, uint32_t size);
void  bFree(void *paddr);
void *bRealloc(void *paddr, uint32_t size);
#endif

uint32_t bGetFreeSize(void);
uint32_t bGetTotalSize(void);

char* bStrDup(char* str);

#if defined(__WEAKDEF)
void bMallocFailedHook(void);
#else
void bMallocRegisterHook(void (*hook)(void));
#endif

#if (defined(_MEMP_WATERLINE_ENABLE) && (_MEMP_WATERLINE_ENABLE == 1))
uint32_t bGetMemThreshold(void);
#endif

/**
 * \}
 */
#endif
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
