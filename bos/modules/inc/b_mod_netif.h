/**
 *!
 * \file        b_mod_netif.h
 * \version     v0.0.1
 * \date        2020/01/02
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO PARAM SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_MOD_NETIF_H__
#define __B_MOD_NETIF_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#define _NETIF_ENABLE 1

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
#include "thirdparty/lwip/bos_lwip/include/lwip/init.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/mem.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/memp.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/netif.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/opt.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/timeouts.h"
#include "thirdparty/lwip/bos_lwip/include/netif/etharp.h"
#include "utils/inc/b_util_list.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup NETIF
 * \{
 */

/**
 * \defgroup NETIF_Exported_TypesDefinitions
 * \{
 */

typedef struct
{
    struct netif     lwip_netif;
    uint32_t         mac_dev;
    int              fd;
    struct list_head list;
} bNetif_t;

/**
 * \}
 */

/**
 * \defgroup NETIF_Exported_Defines
 * \{
 */

#define bNETIF_INSTANCE(name, dev_no) \
    bNetif_t name = {                 \
        .mac_dev = dev_no,            \
    }

#define bNETIF_IP_U32(a, b, c, d)                                                                \
    (((uint32_t)((a)&0xff) << 24) | ((uint32_t)((b)&0xff) << 16) | ((uint32_t)((c)&0xff) << 8) | \
     (uint32_t)((d)&0xff))
/**
 * \}
 */

/**
 * \defgroup NETIF_Exported_Functions
 * \{
 */

// ip eg. 192.168.1.4  bNETIF_IP_U32(192,168,1,4)
int bNetifAdd(bNetif_t *pInstance, uint32_t ip, uint32_t gw, uint32_t mask);

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
