/**
 *!
 * \file        b_type.h
 * \version     v0.0.1
 * \date        2020/05/30
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
#ifndef __B_TYPE_H__
#define __B_TYPE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup TYPE
 * \{
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

#define IS_NULL(p) ((p) == NULL)

#define B_SAFE_INVOKE(f, ...) \
    do                        \
    {                         \
        if ((f) != NULL)      \
        {                     \
            (f)(__VA_ARGS__); \
        }                     \
    } while (0)

#define B_SAFE_INVOKE_RET(ret, f, ...) \
    do                                 \
    {                                  \
        if ((f) != NULL)               \
        {                              \
            ret = (f)(__VA_ARGS__);    \
        }                              \
    } while (0)

#define B_SET_BIT(REG, BIT) ((REG) |= (BIT))
#define B_CLEAR_BIT(REG, BIT) ((REG) &= ~(BIT))
#define B_READ_BIT(REG, BIT) ((REG) & (BIT))
#define B_READ_REG(REG) ((REG))
#define B_WRITE_REG(REG, VAL) ((REG) = (VAL))
#define B_MODIFY_REG(REG, CLEARMASK, SETMASK) \
    B_WRITE_REG((REG), (((B_READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))

#if defined(__CC_ARM)
#define __WEAKDEF __attribute__((weak))
#elif defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
#define __WEAKDEF __attribute__((weak))
#elif defined(__GNUC__)
#define __WEAKDEF __attribute__((weak))
#elif defined(__ICCARM__)
#if __ICCARM_V8
#define __WEAKDEF __attribute__((weak))
#else
#define __WEAKDEF _Pragma("__weak")
#endif
#else
#define __WEAKDEF __attribute__((weak))
#endif

#define B_SIZE_ALIGNMENT(size, align) (((size) + (align)-1) & ~((align)-1))

#define B_OFFSET_OF(type, member) ((uint32_t)(&((type *)0)->member))

#define B_ASSERT(EXPR) \
    if (!(EXPR))       \
    {                  \
        while (1)      \
            ;          \
    }

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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
