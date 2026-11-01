/**
 * \file        test_core.c
 * \brief       Core module tests for BabyOS selftest
 * \author      BabyOS Team
 *
 * Tests: Queue, Semaphore, Timer, Task, Device API
 * NOTE: BabyOS queue/sem/timer require pre-allocated attr structs
 * with embedded buffers — they do not allocate memory themselves.
 *******************************************************************************
 */

#include "../port.h"
#include "b_config.h"
#include "b_os.h"
#include "core/inc/b_queue.h"
#include "core/inc/b_sem.h"
#include "core/inc/b_timer.h"
#include "core/inc/b_task.h"

#if (defined(_UNITY_ENABLE) && (_UNITY_ENABLE == 1))

static volatile int g_timer_fired = 0;
static void _timer_cb(void *arg)
{
    (void)arg;
    g_timer_fired++;
}

/*------------------------------------------------------------
 * Queue tests
 *------------------------------------------------------------*/
void test_bQueueCreate(void)
{
    static uint8_t qbuf[256];
    static bQueueAttr_t q_attr;
    q_attr.mq_mem = qbuf;
    q_attr.mq_size = sizeof(qbuf);

    bQueueId_t q = bQueueCreate(5, sizeof(int), &q_attr);
    TEST_ASSERT_NOT_NULL(q);
    int ret = bQueueDelete(q);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bQueuePutGet(void)
{
    static uint8_t qbuf[256];
    static bQueueAttr_t q_attr;
    q_attr.mq_mem = qbuf;
    q_attr.mq_size = sizeof(qbuf);

    bQueueId_t q = bQueueCreate(5, sizeof(int), &q_attr);
    TEST_ASSERT_NOT_NULL(q);

    int val;
    TEST_ASSERT_EQUAL_INT(-3, bQueueGetNonblock(q, &val));

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(0, bQueuePutNonblock(q, &i));
    }
    TEST_ASSERT_EQUAL_INT(-2, bQueuePutNonblock(q, &(int){99}));

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(0, bQueueGetNonblock(q, &val));
        TEST_ASSERT_EQUAL_INT(i, val);
    }

    bQueueDelete(q);
}

void test_bQueueReset(void)
{
    static uint8_t qbuf[64];
    static bQueueAttr_t q_attr;
    q_attr.mq_mem = qbuf;
    q_attr.mq_size = sizeof(qbuf);

    bQueueId_t q = bQueueCreate(3, sizeof(uint8_t), &q_attr);
    TEST_ASSERT_NOT_NULL(q);

    uint8_t v;
    TEST_ASSERT_EQUAL_INT(0, bQueuePutNonblock(q, &(uint8_t){1}));
    TEST_ASSERT_EQUAL_INT(0, bQueuePutNonblock(q, &(uint8_t){2}));
    TEST_ASSERT_EQUAL_INT(0, bQueueReset(q));
    TEST_ASSERT_EQUAL_INT(-3, bQueueGetNonblock(q, &v));

    bQueueDelete(q);
}

/*------------------------------------------------------------
 * Semaphore tests
 *------------------------------------------------------------*/
void test_bSemCreate(void)
{
    static bSemAttr_t s_attr = { .value = 1, .value_max = 1 };
    bSemId_t s = bSemCreate(1, 1, &s_attr);
    TEST_ASSERT_NOT_NULL(s);
    (void)s; /* no delete API for semaphore */
}

void test_bSemAcquireRelease(void)
{
    static bSemAttr_t s_attr = { .value = 0, .value_max = 1 };
    bSemId_t s = bSemCreate(1, 0, &s_attr);
    TEST_ASSERT_NOT_NULL(s);

    TEST_ASSERT_EQUAL_INT(-1, bSemAcquireNonblock(s));
    TEST_ASSERT_EQUAL_INT(0, bSemRelease(s));
    TEST_ASSERT_EQUAL_UINT32(1, bSemGetCount(s));
    TEST_ASSERT_EQUAL_INT(0, bSemAcquireNonblock(s));
    TEST_ASSERT_EQUAL_UINT32(0, bSemGetCount(s));
    TEST_ASSERT_EQUAL_INT(-1, bSemAcquireNonblock(s));

    (void)s; /* no delete API for semaphore */
}

void test_bSemCounting(void)
{
    static bSemAttr_t s_attr = { .value = 3, .value_max = 3 };
    bSemId_t s = bSemCreate(3, 3, &s_attr);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT32(3, bSemGetCount(s));

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(0, bSemAcquireNonblock(s));
    }
    TEST_ASSERT_EQUAL_UINT32(0, bSemGetCount(s));
    TEST_ASSERT_EQUAL_INT(-1, bSemAcquireNonblock(s));

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(0, bSemRelease(s));
    }
    TEST_ASSERT_EQUAL_UINT32(3, bSemGetCount(s));

    (void)s; /* no delete API for semaphore */
}

/*------------------------------------------------------------
 * Timer tests
 *------------------------------------------------------------*/
void test_bTimerCreate(void)
{
    static bTimerAttr_t t_attr = { .func = _timer_cb, .arg = NULL, .enable = 0 };
    bTimerId_t t = bTimerCreate(_timer_cb, B_TIMER_PERIODIC, NULL, &t_attr);
    TEST_ASSERT_NOT_NULL(t);
    bTimerDelete(t);
}

void test_bTimerStart(void)
{
    g_timer_fired = 0;
    static bTimerAttr_t t_attr = { .func = _timer_cb, .arg = NULL, .enable = 0 };
    bTimerId_t t = bTimerCreate(_timer_cb, B_TIMER_PERIODIC, NULL, &t_attr);
    TEST_ASSERT_NOT_NULL(t);

    bTimerStart(t, 10);
    TEST_ASSERT_EQUAL_INT(1, bTimerIsRunning(t));

    bTimerStop(t);
    TEST_ASSERT_EQUAL_INT(0, bTimerIsRunning(t));

    bTimerDelete(t);
}

/*------------------------------------------------------------
 * Task (protothread) tests
 *------------------------------------------------------------*/
static volatile int g_task_counter = 0;
static volatile int g_task_yield_count = 0;

static char test_task_simple(struct pt *pt, void *arg)
{
    (void)pt; (void)arg;
    g_task_counter = 1;
    return 0;
}

static char test_task_yield(struct pt *pt, void *arg)
{
    (void)pt; (void)arg;
    g_task_yield_count++;
    return 0;
}

void test_bTaskCreate(void)
{
    B_TASK_CREATE_ATTR(task_attr);
    bTaskId_t id = bTaskCreate("test_simple", test_task_simple, NULL, &task_attr);
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_EQUAL_STRING("test_simple", bTaskGetName(id));
    /* Calling create again with same attr returns same id (idempotent) */
    bTaskId_t id2 = bTaskCreate("test_simple2", test_task_simple, NULL, &task_attr);
    TEST_ASSERT_EQUAL_PTR(id, id2);
    bTaskRemove(id);
    TEST_ASSERT_NULL(bTaskGetName(id));
}

void test_bTaskSuspendResume(void)
{
    B_TASK_CREATE_ATTR(task_attr);
    g_task_counter = 0;

    bTaskId_t id = bTaskCreate("test_suspend", test_task_simple, NULL, &task_attr);
    TEST_ASSERT_NOT_NULL(id);

    /* Run task once */
    bExec();
    TEST_ASSERT_EQUAL_INT(1, g_task_counter);

    /* Suspend */
    bTaskSuspend(id);
    g_task_counter = 0;
    bExec();
    TEST_ASSERT_EQUAL_INT(0, g_task_counter); /* suspended, no change */

    /* Resume */
    bTaskResume(id);
    g_task_counter = 0;
    bExec();
    TEST_ASSERT_EQUAL_INT(1, g_task_counter);

    bTaskRemove(id);
}

void test_bTaskYield(void)
{
    B_TASK_CREATE_ATTR(task_attr);
    g_task_yield_count = 0;

    bTaskId_t id = bTaskCreate("test_yield", test_task_yield, NULL, &task_attr);
    TEST_ASSERT_NOT_NULL(id);

    /* Each bExec run increments the counter */
    bExec();
    TEST_ASSERT_EQUAL_INT(1, g_task_yield_count);
    bExec();
    TEST_ASSERT_EQUAL_INT(2, g_task_yield_count);

    bTaskRemove(id);
}

void test_bTaskGetId(void)
{
    B_TASK_CREATE_ATTR(task_attr);
    bTaskId_t id = bTaskCreate("test_getid", test_task_simple, NULL, &task_attr);
    TEST_ASSERT_NOT_NULL(id);
    /* bTaskGetId only valid when called from within a running task */
    bTaskRemove(id);
}

/*------------------------------------------------------------
 * Device API tests
 *------------------------------------------------------------*/
void test_bDeviceNull(void)
{
    /* Opening non-existent device (invalid index) should fail */
    int fd = bOpen(B_REG_DEV_NUMBER, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd < 0);
}

void test_bDeviceFlash(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Erase sector 0 so bytes are 0xFF before writing */
    bFlashErase_t erase = { .addr = 0, .num = 1 };
    TEST_ASSERT_EQUAL_INT(0, bCtl(fd, bCMD_ERASE_SECTOR, &erase));

    uint8_t wbuf[16];
    uint8_t rbuf[16];
    for (int i = 0; i < 16; i++) wbuf[i] = (uint8_t)(i * 0x11);

    TEST_ASSERT_EQUAL_INT(16, bWrite(fd, wbuf, sizeof(wbuf)));
    /* Reset file position so read starts from offset 0 */
    TEST_ASSERT_EQUAL_INT(0, bLseek(fd, 0));
    TEST_ASSERT_EQUAL_INT(16, bRead(fd, rbuf, sizeof(rbuf)));

    bClose(fd); /* must close BEFORE assertion */

    TEST_ASSERT_EQUAL_UINT8_ARRAY(wbuf, rbuf, sizeof(wbuf));
}

void test_bDeviceDuplicateOpen(void)
{
    /* Opening the same device twice should fail (fd table entry already open) */
    int fd1 = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd1 >= 0);

    int fd2 = bOpen(bTESTFLASH, BCORE_FLAG_RW);
    TEST_ASSERT_TRUE(fd2 < 0); /* duplicate open should be rejected */

    bClose(fd1);
}

int test_core_runner(void)
{
    int start = Unity.TestFailures;
    RUN_TEST(test_bQueueCreate);
    RUN_TEST(test_bQueuePutGet);
    RUN_TEST(test_bQueueReset);
    RUN_TEST(test_bSemCreate);
    RUN_TEST(test_bSemAcquireRelease);
    RUN_TEST(test_bSemCounting);
    RUN_TEST(test_bTimerCreate);
    RUN_TEST(test_bTimerStart);
    RUN_TEST(test_bTaskCreate);
    RUN_TEST(test_bTaskSuspendResume);
    RUN_TEST(test_bTaskYield);
    RUN_TEST(test_bTaskGetId);
    RUN_TEST(test_bDeviceNull);
    RUN_TEST(test_bDeviceFlash);
    RUN_TEST(test_bDeviceDuplicateOpen);
    return Unity.TestFailures - start;
}

#endif /* _UNITY_ENABLE */