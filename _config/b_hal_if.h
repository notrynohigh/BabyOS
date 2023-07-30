#ifndef __B_HAL_IF_H__
#define __B_HAL_IF_H__

//----------------------------------------------------------------
//     驱动文件的命名规则 b_drv_<驱动名小写>.c .h
//     每个驱动文件里有宏定义  #define DRIVER_NAME 驱动名大写
//     例如： spiflash驱动
//           驱动文件为 b_drv_spiflash.c .h
//           c文件里面定义宏 #define DRIVER_NAME SPIFLASH
//     驱动需要在此文件（b_hal_if.h）定义HAL层接口
//     #define HAL_<DRIVER_NAME>_IF
//     例如spiflash #define HAL_SPIFLASH_IF {具体的数据接口查看h文件}
//--------------------------------------------------------------------
//     如果有多个spiflash：
//     #define HAL_SPIFLASH_IF {第一个SPIFLASH},{第二个SPIFLASH}
//--------------------------------------------------------------------

#include "b_config.h"

#endif
