#include "b_config.h"
#include "hal/inc/b_hal_wdt.h"



#include "cw32l010_iwdt.h"
#include "cw32l010_sysctrl.h"

/**
 * @brief 启动看门狗
 * @param timeout_s 超时时间（秒），最大建议不超过 60s
 * @return 0:成功, 1:失败
 */
int bMcuWdtStart(uint8_t timeout_s)
{
    IWDT_InitTypeDef IWDT_InitStruct = {0};
    uint32_t reload_value;

    // 1. 确保 LSI 时钟已开启，因为 IWDT 依赖 LSI
    if (CW_SYSCTRL->CR1_f.LSIEN == 0)
    {
        SYSCTRL_LSI_Enable();
    }

    // 2. 计算重装载值
    // 使用 512 分频：频率 = 32000 / 512 = 62.5 Hz
    // reload_value = timeout_s * 62.5
    reload_value = (uint32_t)(timeout_s * 62.5);

    // 限制在寄存器范围内 (12bit, 0x000 ~ 0xFFF)
    if (reload_value > 0xFFF)
    {
        reload_value = 0xFFF;
    }

    // 3. 配置初始化结构体
    IWDT_InitStruct.IWDT_Prescaler = IWDT_Prescaler_DIV512;        // 512分频
    IWDT_InitStruct.IWDT_ReloadValue = reload_value;               // 设置计算出的溢出值
    IWDT_InitStruct.IWDT_OverFlowAction = IWDT_OVERFLOW_ACTION_RESET; // 溢出后复位系统
    IWDT_InitStruct.IWDT_Pause = IWDT_SLEEP_PAUSE;                // 休眠模式下暂停看门狗
    IWDT_InitStruct.IWDT_WindowValue = 0xFFF;                     // 关闭窗口功能（窗口设为最大值）
    IWDT_InitStruct.IWDT_ITState = DISABLE;                       // 直接复位，不使用中断

    // 4. 执行初始化
    if (IWDT_Init(&IWDT_InitStruct) != 0)
    {
        return 1; // 初始化失败（可能是寄存器同步超时）
    }

    // 5. 启动看门狗并喂狗
    IWDT_Cmd();
    IWDT_Refresh();

    return 0;
}


/**
 * @brief 喂狗函数
 */
int bMcuWdtFeed(void)
{
    // 向关键字寄存器写入 0xAAAA 即可喂狗
    IWDT_Refresh(); // 调用底层宏或函数执行喂狗
	return 0;
}


