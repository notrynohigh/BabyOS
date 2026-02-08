#include "b_config.h"
#include "hal/inc/b_hal_wdt.h"
#include "cw32l010.h"

/**
 * @brief 启动看门狗
 * @param timeout_s 超时时间（秒）
 * @return int 0 成功
 */
int bMcuWdtStart(uint8_t timeout_s)
{
    /* CW32L010 IWDT 使用内部 RC 时钟 (LSI)，通常为 32KHz。
       配置步骤：
       1. 写入 0xCCCC 启动 IWDT
       2. 写入 0x5555 允许访问 PR/RLR 寄存器
       3. 设置分频和重装值
    */

    // 1. 启动看门狗
    IWDT->KR = 0xCCCC;

    // 2. 使能寄存器写访问
    IWDT->KR = 0x5555;

    /* 计算超时时间:
       超时时间 = (4 * 2^PR * (RLR + 1)) / LSI
       假设 LSI = 32KHz，分频设为 256 (PR = 6)
       32000 / 256 = 125 Hz
       如果需要 timeout_s 秒，则 RLR = (125 * timeout_s) - 1
    */
    
    // 设置分频为 256 (PR = 6)
    IWDT->PR = 0x06; 

    // 计算重装值，最大 0xFFF (4095)
    // 125 * 32.7 秒 ≈ 4095，所以最大支持约 32 秒
    uint32_t reload = (125 * timeout_s);
    if (reload > 0xFFF)
    {
        reload = 0xFFF;
    }
    else if (reload > 0)
    {
        reload -= 1;
    }

    IWDT->RLR = (uint16_t)(reload & 0xFFF);

    // 3. 等待寄存器更新完成并喂狗重载
    while (IWDT->SR & 0x03); // 等待 PR 和 RLR 更新位清零
    
    IWDT->KR = 0xAAAA; // 重新加载计数值 (喂狗)

    return 0;
}

/**
 * @brief 喂狗函数
 */
void bMcuWdtFeed(void)
{
    // 向关键字寄存器写入 0xAAAA 即可喂狗
    IWDT->KR = 0xAAAA;
}


