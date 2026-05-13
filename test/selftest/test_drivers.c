/**
 * \file        test_drivers.c
 * \brief       Driver tests for BabyOS selftest
 * \author      BabyOS Team
 *
 * Driver tests cover hardware-specific drivers via device API.
 * Tests use bTESTFLASH (mock flash) for storage tests and bKEY (mock GPIO)
 * for input tests.
 *
 * NOTE: Driver tests in this environment use the mock HAL (port.c) which
 * provides software simulation of hardware. Real hardware tests require
 * physical MCU targets.
 *******************************************************************************
 */

#include "../port.h"
#include "b_config.h"
#include "b_os.h"
#include "core/inc/b_device.h"

#if (defined(_UNITY_ENABLE) && (_UNITY_ENABLE == 1))

void setUp(void) {}
void tearDown(void) {}

/*------------------------------------------------------------
 * TestFlash (mock flash) driver tests
 *------------------------------------------------------------*/
void test_bDriverTestFlashOpen(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd >= 0);
    bClose(fd);
}

void test_bDriverTestFlashReadWrite(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Erase sector 0 */
    bFlashErase_t erase = { .addr = 0, .num = 1 };
    TEST_ASSERT_EQUAL_INT(0, bCtl(fd, bCMD_ERASE_SECTOR, &erase));

    /* Write pattern */
    uint8_t wbuf[32];
    uint8_t rbuf[32];
    for (int i = 0; i < 32; i++) wbuf[i] = (uint8_t)(i ^ 0x55);

    TEST_ASSERT_EQUAL_INT(32, bWrite(fd, wbuf, sizeof(wbuf)));

    /* Read back */
    TEST_ASSERT_EQUAL_INT(0, bLseek(fd, 0));
    TEST_ASSERT_EQUAL_INT(32, bRead(fd, rbuf, sizeof(rbuf)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wbuf, rbuf, sizeof(wbuf));

    bClose(fd);
}

void test_bDriverTestFlashSectorSize(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd >= 0);

    uint32_t sector_size = bCtl(fd, bCMD_GET_SECTOR_SIZE, NULL);
    TEST_ASSERT_TRUE(sector_size > 0);

    bClose(fd);
}

void test_bDriverTestFlashSectorCount(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd >= 0);

    uint32_t count = bCtl(fd, bCMD_GET_SECTOR_COUNT, NULL);
    TEST_ASSERT_TRUE(count > 0);

    bClose(fd);
}

void test_bDriverTestFlashMultiSector(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Erase 2 sectors */
    bFlashErase_t erase = { .addr = 0, .num = 2 };
    TEST_ASSERT_EQUAL_INT(0, bCtl(fd, bCMD_ERASE_SECTOR, &erase));

    /* Write 64 bytes */
    uint8_t wbuf[64];
    uint8_t rbuf[64];
    for (int i = 0; i < 64; i++) wbuf[i] = (uint8_t)(i + 1);

    TEST_ASSERT_EQUAL_INT(64, bWrite(fd, wbuf, sizeof(wbuf)));
    TEST_ASSERT_EQUAL_INT(0, bLseek(fd, 0));
    TEST_ASSERT_EQUAL_INT(64, bRead(fd, rbuf, sizeof(rbuf)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wbuf, rbuf, sizeof(rbuf));

    bClose(fd);
}

/*------------------------------------------------------------
 * TestFlash: read-only and write-only flags
 *------------------------------------------------------------*/
void test_bDriverTestFlashReadOnly(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_R);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Write to read-only fd should return -1 */
    uint8_t wbuf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_INT(-1, bWrite(fd, wbuf, sizeof(wbuf)));

    bClose(fd);
}

void test_bDriverTestFlashWriteOnly(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_W);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Read from write-only fd should return -1 */
    uint8_t rbuf[8];
    TEST_ASSERT_EQUAL_INT(-1, bRead(fd, rbuf, sizeof(rbuf)));

    bClose(fd);
}

/*------------------------------------------------------------
 * bKEY (mock GPIO button) driver tests
 *------------------------------------------------------------*/
static volatile uint8_t g_mock_key_level = 0;
static uint8_t _mock_key_read(void)
{
    return g_mock_key_level;
}

void test_bDriverKeyOpen(void)
{
    int fd = bOpen(bKEY, BCORE_FLAG_R);
    TEST_ASSERT_TRUE(fd >= 0);
    bClose(fd);
}

void test_bDriverKeyReadLow(void)
{
    g_mock_key_level = 0;
    bMockGpio_SetIntercept(_mock_key_read, NULL);

    int fd = bOpen(bKEY, BCORE_FLAG_R);
    TEST_ASSERT_TRUE(fd >= 0);

    uint8_t val;
    TEST_ASSERT_EQUAL_INT(1, bRead(fd, &val, 1));
    TEST_ASSERT_EQUAL_UINT8(0, val);

    bClose(fd);
    bMockGpio_ClearIntercept();
}

void test_bDriverKeyReadHigh(void)
{
    g_mock_key_level = 1;
    bMockGpio_SetIntercept(_mock_key_read, NULL);

    int fd = bOpen(bKEY, BCORE_FLAG_R);
    TEST_ASSERT_TRUE(fd >= 0);

    uint8_t val;
    TEST_ASSERT_EQUAL_INT(1, bRead(fd, &val, 1));
    TEST_ASSERT_EQUAL_UINT8(1, val);

    bClose(fd);
    bMockGpio_ClearIntercept();
}

/*------------------------------------------------------------
 * Duplicate open tests
 *------------------------------------------------------------*/
void test_bDriverDuplicateOpen(void)
{
    int fd1 = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd1 >= 0);

    int fd2 = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd2 < 0); /* duplicate open rejected */

    bClose(fd1);
}

/*------------------------------------------------------------
 * Invalid device
 *------------------------------------------------------------*/
void test_bDriverInvalidDevice(void)
{
    int fd = bOpen(B_REG_DEV_NUMBER, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd < 0);
}

/*------------------------------------------------------------
 * Runner
 *------------------------------------------------------------*/
int test_drivers_runner(void)
{
    int start = Unity.TestFailures;
    RUN_TEST(test_bDriverTestFlashOpen);
    RUN_TEST(test_bDriverTestFlashReadWrite);
    RUN_TEST(test_bDriverTestFlashSectorSize);
    RUN_TEST(test_bDriverTestFlashSectorCount);
    RUN_TEST(test_bDriverTestFlashMultiSector);
    RUN_TEST(test_bDriverTestFlashReadOnly);
    RUN_TEST(test_bDriverTestFlashWriteOnly);
    RUN_TEST(test_bDriverKeyOpen);
    RUN_TEST(test_bDriverKeyReadLow);
    RUN_TEST(test_bDriverKeyReadHigh);
    RUN_TEST(test_bDriverDuplicateOpen);
    RUN_TEST(test_bDriverInvalidDevice);
    return Unity.TestFailures - start;
}

#endif /* _UNITY_ENABLE */
