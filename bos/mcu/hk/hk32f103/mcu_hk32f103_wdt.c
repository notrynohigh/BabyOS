#include "b_config.h"
#include "hal/inc/b_hal_wdt.h"

#if !defined(HK32F103C8XX) && !defined(HK32F103CBXX) && !defined(HK32F103R8XX) && \
    !defined(HK32F103RBXX) && !defined(HK32F103V8XX) && !defined(HK32F103VBXX) && \
    !defined(HK32F103RCXX) && !defined(HK32F103RDXX) && !defined(HK32F103REXX) && \
    !defined(HK32F103VCXX) && !defined(HK32F103VDXX) && !defined(HK32F103VEXX)

#error \
    "Please select first the target HK32F10x device used in your application (in HK32f10x.h file)"

#else

#include "hk32f10x.h"

int bMcuWdtStart(uint8_t timeout_s)
{
    B_UNUSED(timeout_s);
    IWDG_SetPrescaler(IWDG_Prescaler_256);
    IWDG_SetReload(0xFFF);
    IWDG_Enable();
    return 0;
}

int bMcuWdtFeed()
{
    IWDG_ReloadCounter();
    return 0;
}

#endif
