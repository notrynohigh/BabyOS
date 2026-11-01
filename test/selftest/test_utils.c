/**
 * \file        test_utils.c
 * \brief       Utils module tests for BabyOS selftest
 * \author      BabyOS Team
 *
 * Tests: FIFO, memory pool, UTC time, Lunar calendar
 *******************************************************************************
 */

#include "../port.h"
#include "b_config.h"
#include "b_os.h"
#include "utils/inc/b_util_fifo.h"
#include "utils/inc/b_util_memp.h"
#include "utils/inc/b_util_utc.h"
#include "utils/inc/b_util_lunar.h"

#if (defined(_UNITY_ENABLE) && (_UNITY_ENABLE == 1))

/*------------------------------------------------------------
 * FIFO tests
 *------------------------------------------------------------*/
void test_bFIFOInit(void)
{
    static uint8_t buf[64];
    static bFIFO_Instance_t fifo;
    int ret = bFIFO_Init(&fifo, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bFIFOWriteRead(void)
{
    static uint8_t buf[16];
    static bFIFO_Instance_t fifo;
    bFIFO_Init(&fifo, buf, sizeof(buf));

    /* Write bytes — returns count written, not 0 */
    uint8_t wdata[] = {0x11, 0x22, 0x33, 0x44};
    int ret = bFIFO_Write(&fifo, wdata, sizeof(wdata));
    TEST_ASSERT_EQUAL_INT(4, ret);

    /* Read back — returns count read */
    uint8_t rdata[4];
    ret = bFIFO_Read(&fifo, rdata, sizeof(rdata));
    TEST_ASSERT_EQUAL_INT(4, ret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wdata, rdata, sizeof(wdata));
}

void test_bFIFOLength(void)
{
    static uint8_t buf[32];
    static bFIFO_Instance_t fifo;
    bFIFO_Init(&fifo, buf, sizeof(buf));

    uint16_t len = 999;
    bFIFO_Length(&fifo, &len);
    TEST_ASSERT_EQUAL_INT(0, len);

    uint8_t v = 0x99;
    bFIFO_Write(&fifo, &v, 1);
    bFIFO_Length(&fifo, &len);
    TEST_ASSERT_EQUAL_INT(1, len);
}

void test_bFIFOFlush(void)
{
    static uint8_t buf[16];
    static bFIFO_Instance_t fifo;
    bFIFO_Init(&fifo, buf, sizeof(buf));

    uint8_t v = 0x55;
    bFIFO_Write(&fifo, &v, 1);
    bFIFO_Flush(&fifo);
    uint16_t len;
    bFIFO_Length(&fifo, &len);
    TEST_ASSERT_EQUAL_INT(0, len);
}

/*------------------------------------------------------------
 * Memory pool tests (uses bMalloc/bFree)
 *------------------------------------------------------------*/
void test_bMemAllocFree(void)
{
    void *p = bMalloc(64);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0xAA, 64);
    uint8_t *cp = (uint8_t *)p;
    TEST_ASSERT_EQUAL_UINT8(0xAA, cp[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, cp[63]);
    bFree(p);
}

void test_bMemSizes(void)
{
    uint32_t total = bGetTotalSize();
    uint32_t free = bGetFreeSize();
    TEST_ASSERT_TRUE(total > 0);
    TEST_ASSERT_TRUE(free <= total);
}

void test_bCalloc(void)
{
    void *p = bCalloc(8, 4); /* 8 elements of 4 bytes each */
    TEST_ASSERT_NOT_NULL(p);
    uint8_t *cp = (uint8_t *)p;
    TEST_ASSERT_EQUAL_UINT8(0, cp[0]);
    TEST_ASSERT_EQUAL_UINT8(0, cp[31]);
    bFree(p);
}

/*------------------------------------------------------------
 * UTC time tests
 *------------------------------------------------------------*/
void test_bUTCStructConversion(void)
{
    bUTC_t ts = 1577836800LL; /* 2020-01-01 00:00:00 UTC */
    bUTC_DateTime_t dt;
    bUTC2Struct(&dt, ts, 0.0);
    TEST_ASSERT_EQUAL_INT(2020, dt.year);
    TEST_ASSERT_EQUAL_INT(1, dt.month);
    TEST_ASSERT_EQUAL_INT(1, dt.day);
    TEST_ASSERT_EQUAL_INT(0, dt.hour);
    TEST_ASSERT_EQUAL_INT(0, dt.minute);
    TEST_ASSERT_EQUAL_INT(0, dt.second);
}

void test_bUTCSetGet(void)
{
    bUTC_t ts_before = bUTC_GetTime();
    TEST_ASSERT_TRUE(ts_before >= 0);

    bUTC_t new_ts = 1609459200LL; /* 2021-01-01 00:00:00 UTC */
    bUTC_SetTime(new_ts);
    bUTC_t ts_after = bUTC_GetTime();
    TEST_ASSERT_EQUAL_INT64(new_ts, ts_after);

    /* Restore time to avoid breaking other tests */
    bUTC_SetTime(ts_before);
}

void test_bUTCTimestampRoundTrip(void)
{
    bUTC_DateTime_t dt = {
        .year = 2020, .month = 7, .day = 4,
        .hour = 12, .minute = 30, .second = 45
    };
    bUTC_t ts = bStruct2UTC(dt, 0.0);
    TEST_ASSERT_TRUE(ts > 0);

    bUTC_DateTime_t dt2;
    bUTC2Struct(&dt2, ts, 0.0);
    TEST_ASSERT_EQUAL_INT(2020, dt2.year);
    TEST_ASSERT_EQUAL_INT(7, dt2.month);
    TEST_ASSERT_EQUAL_INT(4, dt2.day);
}

/*------------------------------------------------------------
 * Lunar calendar tests
 *------------------------------------------------------------*/
void test_bLunarCalendar(void)
{
    bLunarInfo_t lunar;
    /* 2021-02-12 → Chinese New Year 2021 */
    int ret = bSolar2Lunar(2021, 2, 12, &lunar);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(1, lunar.month);  /* First lunar month */
    TEST_ASSERT_EQUAL_INT(1, lunar.day);    /* First day */
}

/*------------------------------------------------------------
 * Tools tests (bParseString)
 *------------------------------------------------------------*/
void test_bParseString(void)
{
    const char *str = "key=123";
    const char *result = NULL;
    int32_t num = -1;
    /* Parse index 0 ("key"), index 1 ("=123") */
    int ret = bParseString(str, "=", 1, &num, &result);
    (void)ret;
    (void)result;
    /* num should be parsed from "=123" → 123 */
    TEST_ASSERT_EQUAL_INT32(123, num);
}

int test_utils_runner(void)
{
    int start = Unity.TestFailures;
    RUN_TEST(test_bFIFOInit);
    RUN_TEST(test_bFIFOWriteRead);
    RUN_TEST(test_bFIFOLength);
    RUN_TEST(test_bFIFOFlush);
    RUN_TEST(test_bMemAllocFree);
    RUN_TEST(test_bMemSizes);
    RUN_TEST(test_bCalloc);
    RUN_TEST(test_bUTCStructConversion);
    RUN_TEST(test_bUTCSetGet);
    RUN_TEST(test_bUTCTimestampRoundTrip);
    RUN_TEST(test_bLunarCalendar);
    RUN_TEST(test_bParseString);
    return Unity.TestFailures - start;
}

#endif /* _UNITY_ENABLE */
