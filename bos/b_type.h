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

/**
 * \defgroup B_SAFE_INVOKE 安全函数指针调用
 *
 * \brief 在调用函数指针前显式检查 NULL, 避免解引用 NULL 触发 HardFault (Cortex-M)
 *        或 SIGSEGV (host). 用于:
 *        - 驱动可选回调 (WiFi mode change, link state cb)
 *        - 协议栈未初始化时 stub 调用 (lwIP default_netif)
 *        - 用户注册但允许跳过的事件 handler
 *
 * \param f     函数指针 (可为 NULL, NULL 时宏展开为空)
 * \param ...   透传给 f 的实参
 *
 * \note 当 f 为 NULL 时, 宏整体展开为空 do-while, 调用方代码继续执行, 不返错.
 *       调用方负责"f=NULL 时怎么办"的语义: 通常是无操作 (no-op), 但也可能意味着
 *       协议栈未初始化 (见 b_mod_tcpip.c 中 lwIP 路径).
 *
 * \warning 不要在中断上下文频繁使用 — NULL 检查 + 间接调用会比直接调用慢 5~10 ns.
 *          MCU 上 1 MHz Cortex-M0 这 10 ns 是 ~10 个 cycle, 实测无影响.
 *
 * \code
 *   // 例子: 驱动通知用户协议栈事件
 *   B_SAFE_INVOKE(pinfo->stack_if.set_default_netif, &pinfo->netif);
 * \endcode
 */
#define B_SAFE_INVOKE(f, ...) \
    do                        \
    {                         \
        if ((f) != NULL)      \
        {                     \
            (f)(__VA_ARGS__); \
        }                     \
    } while (0)

/**
 * \brief 同 B_SAFE_INVOKE, 但保留函数返回值到 \p ret. 用于回调可能修改状态变量时.
 *
 * \param ret   接收返回值的左值 (例如 int rc = 0;)
 * \param f     函数指针
 * \param ...   透传给 f 的实参
 *
 * \note f 为 NULL 时 ret 不被赋值, 保持调用方预设值.
 */
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
#define __TIME_F_ENABLE 1
#define __USE_LOGF 1
#elif defined(__GNUC__)
#define __WEAKDEF __attribute__((weak))
#elif defined(__ICCARM__)
#define __WEAKDEF __weak
#elif defined(__RENESAS__)
#else
#define __WEAKDEF __attribute__((weak))
#define __TIME_F_ENABLE 1
#endif

#if defined(__RENESAS__)
#define __INLINE_DEF __inline
#else
#define __INLINE_DEF inline
#endif

#define B_SIZE_ALIGNMENT(size, align) (((size) + (align)-1) & ~((align)-1))

#define B_OFFSET_OF(type, member) ((uint32_t)(&((type *)0)->member))

#define B_ASSERT(EXPR) \
    if (!(EXPR))       \
    {                  \
        while (1)      \
            ;          \
    }

#define B_SWAP_16(x) (((x) >> 8) | ((x) << 8))
#define B_SWAP_32(x)                                                                      \
    ((((x) & 0xFF000000) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | \
     (((x) & 0x000000FF) << 24))
#define B_SWAP_64(x)                                                                 \
    ((((x) & 0xFF00000000000000ULL) >> 56) | (((x) & 0x00FF000000000000ULL) >> 40) | \
     (((x) & 0x0000FF0000000000ULL) >> 24) | (((x) & 0x000000FF00000000ULL) >> 8) |  \
     (((x) & 0x00000000FF000000ULL) << 8) | (((x) & 0x0000000000FF0000ULL) << 24) |  \
     (((x) & 0x000000000000FF00ULL) << 40) | (((x) & 0x00000000000000FFULL) << 56))

#define B_UNUSED(x) (void)(x)

#define B_ALIGN_TO_2_BYTES(addr) ((((uintptr_t)(addr)) + 1) & ~((uintptr_t)1))

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
