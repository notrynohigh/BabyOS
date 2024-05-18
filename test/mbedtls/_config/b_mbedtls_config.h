#ifndef __B_MBEDTLS_CONFIG_H__
#define __B_MBEDTLS_CONFIG_H__

#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_STD_CALLOC bCalloc
#define MBEDTLS_PLATFORM_STD_FREE bFree

#define MBEDTLS_AES_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_MD_C
#define MBEDTLS_DHM_C
#define MBEDTLS_GENPRIME
#define MBEDTLS_AES_ROM_TABLES

#endif
