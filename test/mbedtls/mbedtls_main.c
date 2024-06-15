/**
 * \file kv_main.c
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */
#include "../port.h"
#include "b_os.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/bignum.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/ctr_drbg.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/entropy.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/platform.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/rsa.h"

static int entropy_source(void *data, uint8_t *output, uint32_t len, uint32_t *olen)
{
    uint32_t seed;
    seed = bHalRNGRead();
    if (len > sizeof(seed))
    {
        len = sizeof(seed);
    }
    memcpy(output, &seed, len);
    *olen = len;
    return 0;
}

static void random_test()
{
    uint8_t                  random[64];
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char              *pers = "CTR_DRBG";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    mbedtls_entropy_add_source(&entropy, entropy_source, NULL, MBEDTLS_ENTROPY_MAX_GATHER,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, pers, strlen(pers));

    b_log("----mbedtls rng--- setup ok \r\n");

    mbedtls_ctr_drbg_random(&ctr_drbg, random, sizeof(random));
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    b_log_hex(random, sizeof(random));
}

static void bignum_prime_test()
{
    uint8_t                  prime[64];
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char              *pers = "CTR_DRBG";
    mbedtls_mpi              P, Q;

    mbedtls_mpi_init(&P);
    mbedtls_mpi_init(&Q);

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    mbedtls_entropy_add_source(&entropy, entropy_source, NULL, MBEDTLS_ENTROPY_MAX_GATHER,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, pers, strlen(pers));

    b_log("----mbedtls rng--- setup ok \r\n");

    mbedtls_mpi_gen_prime(&P, sizeof(prime) * 8, 1, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_mpi_sub_int(&Q, &P, 1);
    mbedtls_mpi_div_int(&Q, NULL, &Q, 2);

    if (0 == mbedtls_mpi_is_prime_ext(&Q, 10, mbedtls_ctr_drbg_random, &ctr_drbg))
    {
        b_log("verify ok...\r\n");
    }
    else
    {
        b_log_e("verify fail...\r\n");
    }

    mbedtls_mpi_write_binary(&P, prime, sizeof(prime));

    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&Q);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    b_log_hex(prime, sizeof(prime));
}

static void rsa_test()
{
    uint8_t                  out[2048 / 8];
    mbedtls_rsa_context      ctx;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char              *pers = "CTR_DRBG";
    const char              *msg  = "hello world";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_rsa_init(&ctx);
    mbedtls_rsa_set_padding(&ctx, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

    mbedtls_entropy_add_source(&entropy, entropy_source, NULL, MBEDTLS_ENTROPY_MAX_GATHER,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, pers, strlen(pers));

    b_log("----mbedtls rng--- setup ok \r\n");

    mbedtls_rsa_gen_key(&ctx, mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);
    mbedtls_rsa_pkcs1_encrypt(&ctx, mbedtls_ctr_drbg_random, &ctr_drbg, strlen(msg), msg, out);
    uint32_t olen = 0;
    mbedtls_rsa_pkcs1_decrypt(&ctx, mbedtls_ctr_drbg_random, &ctr_drbg, &olen, out, out,
                              sizeof(out));
    b_log("olen:%d\n", olen);
    out[olen] = 0;
    b_log("out:%s\r\n", out);

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_rsa_free(&ctx);
}

int main()
{
    port_init();
    bInit();
    random_test();
    bHalDelayMs(1000);
    bignum_prime_test();
    bHalDelayMs(1000);
    rsa_test();
    while (1)
    {
        bExec();
    }
    return 0;
}
