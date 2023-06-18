/**
 *!
 * \file        b_mod_select.c
 * \version     v0.0.1
 * \date        2020/05/16
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
#include "modules/inc/b_mod_select.h"

#if (defined(_SELECT_ENABLE) && (_SELECT_ENABLE == 1))

#include "core/inc/b_core.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup SELECT
 * \{
 */

/**
 * \defgroup SELECT_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SELECT_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SELECT_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SELECT_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SELECT_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SELECT_Private_Functions
 * \{
 */

/**
 * \}
 */

/**
 * \addtogroup SELECT_Exported_Functions
 * \{
 */

int bSelect(int maxfdp, bFdSet_t *readfds, bFdSet_t *writefds, bFdSet_t *errorfds)
{
    int      i     = 0;
    int      count = 0;
    bFdSet_t tmp;
    if (readfds == NULL && writefds == NULL && errorfds == NULL)
    {
        return -1;
    }
    memset(&tmp, 0, sizeof(bFdSet_t));
    maxfdp = (maxfdp > SELECT_FDS_NUMBER) ? SELECT_FDS_NUMBER : maxfdp;
    if (readfds)
    {
        for (i = 0; i < maxfdp; i++)
        {
            if (readfds->fds[i / 8] & (0x1 << (i % 8)))
            {
                if (bFdIsReadable(i))
                {
                    count += 1;
                    tmp.fds[i / 8] |= 0x1 << (i % 8);
                }
            }
        }
        if (count > 0)
        {
            memcpy(readfds, &tmp, sizeof(bFdSet_t));
            memset(&tmp, 0, sizeof(bFdSet_t));
        }
    }

    if (writefds)
    {
        for (i = 0; i < maxfdp; i++)
        {
            if (writefds->fds[i / 8] & (0x1 << (i % 8)))
            {
                if (bFdIsWritable(i))
                {
                    count += 1;
                    tmp.fds[i / 8] |= 0x1 << (i % 8);
                }
            }
        }
        if (count > 0)
        {
            memcpy(writefds, &tmp, sizeof(bFdSet_t));
            memset(&tmp, 0, sizeof(bFdSet_t));
        }
    }

    if (errorfds)
    {
        for (i = 0; i < maxfdp; i++)
        {
            if (errorfds->fds[i / 8] & (0x1 << (i % 8)))
            {
                if (bFdIsAbnormal(i))
                {
                    count += 1;
                    tmp.fds[i / 8] |= 0x1 << (i % 8);
                }
            }
        }
        if (count > 0)
        {
            memcpy(errorfds, &tmp, sizeof(bFdSet_t));
            memset(&tmp, 0, sizeof(bFdSet_t));
        }
    }
    return count;
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

/**
 * \}
 */
#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
