/**
 *!
 * \file        b_mod_select.h
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
#ifndef __B_MOD_SELECT_H__
#define __B_MOD_SELECT_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_SELECT_ENABLE) && (_SELECT_ENABLE == 1))

#include "thirdparty/pt/pt.h"

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
 * \defgroup SELECT_Exported_TypesDefinitions
 * \{
 */

typedef struct
{
    uint8_t fds[(SELECT_FDS_NUMBER + 7) / 8];
} bFdSet_t;

/**
 * \}
 */

/**
 * \defgroup SELECT_Exported_Defines
 * \{
 */

#define bFD_ZERO(pfds)                           \
    do                                           \
    {                                            \
        if ((pfds) != NULL)                      \
        {                                        \
            memset((pfds), 0, sizeof(bFdSet_t)); \
        }                                        \
    } while (0)

#define bFD_SET(fd, pfds)                               \
    do                                                  \
    {                                                   \
        if ((fd) < SELECT_FDS_NUMBER && (pfds) != NULL) \
        {                                               \
            (pfds)->fds[(fd) / 8] |= 0x1 << ((fd) % 8); \
        }                                               \
    } while (0)

#define bFD_ISSET(fd, pfds) ((pfds)->fds[(fd) / 8] & (0x1 << ((fd) % 8)))

#define B_PT_SELECT(pt, maxfdp, readfds, writefds, errorfds, timeout, result) \
    PT_WAIT_UNTIL(pt, (result = bSelect(maxfdp, readfds, writefds, errorfds)) > 0, timeout)

/**
 * \}
 */

/**
 * \defgroup SELECT_Exported_Functions
 * \{
 */

// bSelect是非阻塞的，如果要做到阻塞的效果，建议开启pt,并使用B_PT_SELECT
int bSelect(int maxfdp, bFdSet_t *readfds, bFdSet_t *writefds, bFdSet_t *errorfds);

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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
