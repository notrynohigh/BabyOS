/**
 *!
 * \file        b_hal_eth.h
 * \version     v0.0.1
 * \date        2021/06/13
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2021 Bean
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SGPIOL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_HAL_ETH_H__
#define __B_HAL_ETH_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup ETH
 * \{
 */

/**
 * \defgroup ETH_Exported_TypesDefinitions
 * \{
 */

// 为了减少峰值内存，减少数据在不同层的拷贝。将上层操作struct pbuf的方法注册给下层使用
typedef struct
{
    // m_create 申请空间
    // len: 申请空间的长度
    // p :  申请空间的指针
    int (*m_create)(uint16_t len, void **p);

    // m_next 申请的空间是链表形式，调用这个接口切换到下一块空间
    // current_p: 指向当前使用的空间
    // p :  得到的下一个空间的指针
    int (*m_next)(void *current_p, void **p);

    // m_payload 获取payload空间。
    // p : 指向当前空间的指针
    // payload: 空间可能存在头部，那么payload指向实际数据空间
    // paylaod_len: 可用空间长度
    int (*m_payload)(void *p, void **payload, uint32_t *payload_len);
} bHalBufList_t;
/**
 * \}
 */

/**
 * \defgroup ETH_Exported_Functions
 * \{
 */
int     bMcuEthInit(bHalBufList_t *pbuf_list);
int     bMcuEthGetMacAddr(uint8_t *paddr, uint8_t len);
uint8_t bMcuEthIsLinked(void);
int     bMcuEthLinkUpdate(uint8_t link_state);
int     bMcuEthReceive(void **pbuf, uint32_t *plen);
int     bMcuEthTransmit(void *pbuf, uint32_t len);
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
int     bHalEthInit(bHalBufList_t *pbuf_list);
int     bHalEthGetMacAddr(uint8_t *paddr, uint8_t len);
uint8_t bHalEthIsLinked(void);
int     bHalEthLinkUpdate(uint8_t link_state);
int     bHalEthReceive(void **pbuf, uint32_t *plen);
int     bHalEthTransmit(void *pbuf, uint32_t len);
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

/************************ Copyright (c) 2021 Bean *****END OF FILE****/
