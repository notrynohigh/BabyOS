#include "b_config.h"
#include "hal/inc/b_hal_wdt.h"

#include "py32f0xx.h"

/**
 * @brief 启动独立看门狗
 * @param timeout_s 超时时间（秒）。
 * 注：由于 IWDG 配置较为固定，本实现主要参考原 HK32 代码逻辑。
 */
int bMcuWdtStart(uint8_t timeout_s)
{
    /* 1. 使能寄存器访问（写允许） */
    IWDG->KR = 0x5555; 

    /* 2. 设置分频系数：LSI (32.768kHz) / 256 = 128Hz */
    /* IWDG_PRESCALER_256 对应的寄存器值通常为 0x06 */
    IWDG->PR = 0x06; 

    /* 3. 设置重装载值：0xFFF (4095) */
    /* 超时时间 = 4095 / 128Hz ≈ 32 秒 */
    IWDG->RLR = 0x0FFF; 

    /* 4. 重载计数器并启动 */
    IWDG->KR = 0xAAAA; // 喂狗指令
    IWDG->KR = 0xCCCC; // 启动看门狗指令

    return 0;
}

/**
 * @brief 喂狗（重载看门狗计数器）
 */
int bMcuWdtFeed()
{
    /* 向键寄存器写入 0xAAAA 触发重载 */
    IWDG->KR = 0xAAAA;
    return 0;
}

