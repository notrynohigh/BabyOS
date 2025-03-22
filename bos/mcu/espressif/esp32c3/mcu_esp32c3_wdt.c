#include "b_config.h"
#include "esp_task_wdt.h"
#include "hal/inc/b_hal_wdt.h"

int bMcuWdtStart(uint8_t timeout_s)
{
    if (timeout_s == 0)
    {
        return -1;
    }
    esp_task_wdt_config_t config;
    config.timeout_ms     = timeout_s * 1000;
    config.trigger_panic  = true;
    config.idle_core_mask = (1 << 0);
    esp_err_t ret         = esp_task_wdt_init(&config);
    if (ret != ESP_OK)
    {
        return -1;
    }
    ret = esp_task_wdt_add(NULL);  // NULL 表示当前任务
    if (ret != ESP_OK)
    {
        return -1;
    }
    return 0;
}

int bMcuWdtFeed()
{
    esp_err_t ret = esp_task_wdt_reset();
    if (ret != ESP_OK)
    {
        return -1;
    }
    return 0;
}
