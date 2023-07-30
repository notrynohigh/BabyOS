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
/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup TYPE
 * \{
 */

typedef unsigned char     uint8_t;
typedef unsigned int      uint16_t;
typedef unsigned long int uint32_t;

typedef signed char     int8_t;
typedef signed int      int16_t;
typedef signed long int int32_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define IS_NULL(p) ((p) == NULL)

#define B_SET_BIT(REG, BIT) ((REG) |= (BIT))
#define B_CLEAR_BIT(REG, BIT) ((REG) &= ~(BIT))
#define B_READ_BIT(REG, BIT) ((REG) & (BIT))
#define B_READ_REG(REG) ((REG))
#define B_WRITE_REG(REG, VAL) ((REG) = (VAL))
#define B_MODIFY_REG(REG, CLEARMASK, SETMASK) \
    B_WRITE_REG((REG), (((B_READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))

#define __WEAKDEF

#define B_SIZE_ALIGNMENT(size, align) (((size) + (align)-1) & ~((align)-1))

#define B_OFFSET_OF(type, member) ((uint32_t)(&((type *)0)->member))

#define B_MEM_SET(p, v, l)             \
    do                                 \
    {                                  \
        for (i = 0; i < l; i++)        \
        {                              \
            ((uint8_t *)(p))[i] = (v); \
        }                              \
    } while (0)

#define B_MEM_COPY(des, src, len)                          \
    do                                                     \
    {                                                      \
        for (i = 0; i < (len); i++)                        \
        {                                                  \
            ((uint8_t *)(des))[i] = ((uint8_t *)(src))[i]; \
        }                                                  \
    } while (0)

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
