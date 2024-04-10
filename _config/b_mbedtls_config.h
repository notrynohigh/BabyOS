#ifndef __B_MBEDTLS_CONFIG_H__
#define __B_MBEDTLS_CONFIG_H__

#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_STD_CALLOC bCalloc
#define MBEDTLS_PLATFORM_STD_FREE bFree

#endif
