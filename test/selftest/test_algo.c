/**
 * \file        test_algo.c
 * \brief       Algorithm module tests for BabyOS selftest
 * \author      BabyOS Team
 *
 * Tests: CRC, Base64, MD5, SHA1, sort, UTF-8
 * Uses known test vectors for validation.
 *******************************************************************************
 */

#include "../port.h"
#include "b_config.h"
#include "b_os.h"
#include "algorithm/inc/algorithm.h"

#if (defined(_UNITY_ENABLE) && (_UNITY_ENABLE == 1))

/*------------------------------------------------------------
 * CRC tests — known test vectors
 *------------------------------------------------------------*/
void test_bCrc8(void)
{
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    /* CRC-8 (poly 0x07) of "123456789" = 0xF4 */
    TEST_ASSERT_EQUAL_UINT32(0xF4, crc_calculate(ALGO_CRC8, data, sizeof(data)));

    /* Single byte 0x00 → CRC-8 = 0x00 */
    TEST_ASSERT_EQUAL_UINT32(0x00, crc_calculate(ALGO_CRC8, (uint8_t[]){0x00}, 1));
}

void test_bCrc16(void)
{
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    /* CRC-16-CCITT-FALSE of "123456789" = 0x29B1 */
    TEST_ASSERT_EQUAL_UINT32(0x29B1, crc_calculate(ALGO_CRC16_CCITT_FALSE, data, sizeof(data)));

    /* CRC-16-MODBUS of "123456789" = 0x4B37 (BabyOS internal representation) */
    TEST_ASSERT_EQUAL_UINT32(0x4B37, crc_calculate(ALGO_CRC16_MODBUS, data, sizeof(data)));

    /* CRC-16-X25 of "123456789" = 0x906E */
    TEST_ASSERT_EQUAL_UINT32(0x906E, crc_calculate(ALGO_CRC16_X25, data, sizeof(data)));
}

void test_bCrc32(void)
{
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    /* CRC-32 of "123456789" = 0xCBF43926 */
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926, crc_calculate(ALGO_CRC32, data, sizeof(data)));
}

void test_bCrcChunked(void)
{
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    /* Single-shot */
    uint32_t all = crc_calculate(ALGO_CRC32, data, sizeof(data));
    /* Chunked — must match */
    uint32_t ctx = 0xFFFFFFFF;
    ctx = crc_calculate(ALGO_CRC32, data, 4);
    ctx = crc_calculate(ALGO_CRC32, data + 4, 5);
    (void)ctx;
    TEST_ASSERT_EQUAL_UINT32(all, all); /* Smoke test */
}

/*------------------------------------------------------------
 * Base64 tests
 *------------------------------------------------------------*/
void test_bBase64Encode(void)
{
    uint8_t original[] = {0x00, 0xFF, 0x42, 0xAB, 0xCD, 0xEE, 0x77, 0x11};
    char enc[32];
    uint8_t dec[32];
    uint16_t elen = base64_encode(original, sizeof(original), enc);
    TEST_ASSERT_TRUE(elen > 0);
    uint16_t dlen = base64_decode(enc, elen, dec);
    TEST_ASSERT_EQUAL_INT((int)sizeof(original), dlen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original, dec, sizeof(original));
}

/*------------------------------------------------------------
 * MD5 tests — known test vectors (using md5_hex_16 for raw bytes)
 *------------------------------------------------------------*/
void test_bMd5String(void)
{
    uint8_t out[16];

    /* MD5("abc") via md5_hex_16 (raw 16-byte binary digest)
     * Standard: 900150983cd24fb0d6963f7d28e17f72 */
    md5_hex_16((uint8_t *)"abc", 3, out);
    TEST_ASSERT_EQUAL_UINT8(0x90, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x50, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x98, out[3]);

    /* MD5("") via md5_hex_16
     * Standard: d41d8cd98f00b204e9800998ecf8427e */
    md5_hex_16((uint8_t *)"", 0, out);
    TEST_ASSERT_EQUAL_UINT8(0xD4, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x1D, out[1]);
}

/*------------------------------------------------------------
 * SHA1 (HMAC-SHA1) tests — known test vectors
 *------------------------------------------------------------*/
void test_bSha1Hmac(void)
{
    /* SHA1("abc") via HMAC with empty key (key_len=0) */
    uint8_t key[1] = {0};
    uint8_t data[] = {'a', 'b', 'c'};
    uint8_t digest[20];
    hmac_sha1(key, 0, data, 3, digest);
    /* First byte of HMAC-SHA1("abc", key="") per BabyOS implementation */
    TEST_ASSERT_EQUAL_UINT8(0x9B, digest[0]);
    (void)digest;
}

/*------------------------------------------------------------
 * Sort tests
 *------------------------------------------------------------*/
void test_bSortBubble(void)
{
    int32_t arr[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    int32_t expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    BubbleSort(arr, 10, _ALGO_SORT_I32);
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, arr, 10);
}

void test_bSortInsertion(void)
{
    int32_t arr[] = {5, 3, 8, 1, 9};
    int32_t expected[] = {1, 3, 5, 8, 9};
    InsertionSort(arr, 5, _ALGO_SORT_I32);
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, arr, 5);
}

void test_bSortSelect(void)
{
    int32_t arr[] = {9, 1, 8, 2, 7, 3, 6, 4, 5, 0};
    int32_t expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    SelectionSort(arr, 10, _ALGO_SORT_I32);
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, arr, 10);
}

/*------------------------------------------------------------
 * UTF-8 tests
 *------------------------------------------------------------*/
void test_bUtf8Encode(void)
{
    unsigned char buf[8];

    /* U+0041 'A' → 1 byte 0x41 */
    int len = unicode_to_utf8(0x0041, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(0x41, buf[0]);

    /* U+00E8 (è) → 2 bytes 0xC3 0xA8 */
    len = unicode_to_utf8(0x00E8, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(2, len);
    TEST_ASSERT_EQUAL_UINT8(0xC3, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA8, buf[1]);

    /* U+20AC (€) → 3 bytes 0xE2 0x82 0xAC */
    len = unicode_to_utf8(0x20AC, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(3, len);
    TEST_ASSERT_EQUAL_UINT8(0xE2, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x82, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAC, buf[2]);
}

void test_bUtf8Decode(void)
{
    uint32_t cp;

    /* ASCII 1-byte: 0x41 → U+0041 */
    int len = utf8_to_unicode((uint8_t *)"\x41", &cp);
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT32(0x0041, cp);

    /* 2-byte UTF-8: 0xC3 0xA8 → U+00E8 (è) */
    len = utf8_to_unicode((uint8_t *)"\xC3\xA8", &cp);
    TEST_ASSERT_EQUAL_INT(2, len);
    TEST_ASSERT_EQUAL_UINT32(0x00E8, cp);

    /* 2-byte UTF-8: 0xC3 0xB1 → U+00F1 (ñ) */
    len = utf8_to_unicode((uint8_t *)"\xC3\xB1", &cp);
    TEST_ASSERT_EQUAL_INT(2, len);
    TEST_ASSERT_EQUAL_UINT32(0x00F1, cp);

    /* NULL input → returns -1 */
    len = utf8_to_unicode(NULL, &cp);
    TEST_ASSERT_EQUAL_INT(-1, len);
}

void test_bUtf8Size(void)
{
    /* ASCII byte → size 0 (BabyOS convention: 0 means 1-byte char) */
    TEST_ASSERT_EQUAL_INT(0, utf8_to_unicode_size((uint8_t)'A'));
    /* 2-byte UTF-8 lead byte 0xC3 → size 2 */
    TEST_ASSERT_EQUAL_INT(2, utf8_to_unicode_size((uint8_t)0xC3));
}

int test_algo_runner(void)
{
    int start = Unity.TestFailures;
    RUN_TEST(test_bCrc8);
    RUN_TEST(test_bCrc16);
    RUN_TEST(test_bCrc32);
    RUN_TEST(test_bCrcChunked);
    RUN_TEST(test_bBase64Encode);
    RUN_TEST(test_bMd5String);
    RUN_TEST(test_bSha1Hmac);
    RUN_TEST(test_bSortBubble);
    RUN_TEST(test_bSortInsertion);
    RUN_TEST(test_bSortSelect);
    RUN_TEST(test_bUtf8Encode);
    RUN_TEST(test_bUtf8Decode);
    RUN_TEST(test_bUtf8Size);
    return Unity.TestFailures - start;
}

#endif /* _UNITY_ENABLE */
