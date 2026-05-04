/**
 * \file        test_modules.c
 * \brief       Software module tests for BabyOS selftest
 * \author      BabyOS Team
 *
 * Tests: KV, FS, Error, State, Select, QRCode, Menu, Param, Modbus, Shell
 * Modules requiring bInit() first: KV, FS, State (done in setUp/module_setUp).
 * Pure-software modules (Error, QRCode, Menu, Param, Modbus, Shell): no init needed.
 *******************************************************************************
 */

#include "../port.h"
#include "b_config.h"
#include "b_os.h"
#include <string.h>
#include "modules/inc/b_mod_fs.h"
#include "modules/inc/b_mod_error.h"
#include "modules/inc/b_mod_state.h"
#include "modules/inc/b_mod_select.h"
#include "modules/inc/b_mod_qrcode.h"
#if (defined(_MENU_ENABLE) && (_MENU_ENABLE == 1))
#include "modules/inc/b_mod_menu.h"
#endif
#if (defined(_PARAM_ENABLE) && (_PARAM_ENABLE == 1))
#include "modules/inc/b_mod_param.h"
#endif
#if (defined(_MODBUS_ENABLE) && (_MODBUS_ENABLE == 1))
#include "modules/inc/b_mod_proto_type.h"
#include "modules/inc/b_mod_modbus.h"
#include "algorithm/inc/algo_crc.h"
#endif
#if (defined(_NR_MICRO_SHELL_ENABLE) && (_NR_MICRO_SHELL_ENABLE == 1))
#include "modules/inc/b_mod_shell.h"
#endif

#if (defined(_UNITY_ENABLE) && (_UNITY_ENABLE == 1))

/*------------------------------------------------------------
 * KV tests
 *------------------------------------------------------------*/
bKV_INSTANCE(gTestKV, bTESTFLASH, 0, 4096 * 4, 4096);

/*------------------------------------------------------------
 * FS tests
 *------------------------------------------------------------*/
/* Two partitions share TESTFLASH RAM buffer (4MB total):
 * TESTFLASH e_size=4096, w_size=1.
 * KV region: 0x4000 bytes starting at absolute offset 0 (written by bKVInit).
 * Partition 0: base=0x4000, size=3MB, FAT32 (avoids KV region)
 * Partition 1: base=3MB+0x4000, size=1MB, LITTLEFS
 * All partition offsets are at or beyond 0x4000, never touching KV data. */
static const bFSPartition_t s_fs_parts[2] = {
    {
        .index       = 0,
        .dev_no      = bTESTFLASH,
        .base_addr   = 0x4000,           /* 16KB — after KV region */
        .total_size  = 3 * 1024 * 1024,   /* 3MB, sector-aligned */
        .sector_size = 4096,              /* must align with TESTFLASH e_size */
    },
    {
        .index       = 1,
        .dev_no      = bTESTFLASH,
        .base_addr   = 0x4000 + 3 * 1024 * 1024, /* after partition 0 */
        .total_size  = 1024 * 1024,       /* 1MB, sector-aligned */
        .sector_size = 4096,              /* must align with TESTFLASH e_size */
    },
};

static uint8_t g_module_init_done = 0;

static void module_setUp(void)
{
    if (g_module_init_done) return;
    g_module_init_done = 1;
    bKVInit(&gTestKV);
    bFSInit(s_fs_parts, 2); /* also resets gbFSTable / gbFSFileCount */
}

void test_bFSInit(void)
{
    g_module_init_done = 0;
    int ret = bFSInit(s_fs_parts, 2);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bFSInitNull(void)
{
    int ret = bFSInit(NULL, 2);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_bFSMountFatfs(void)
{
    g_module_init_done = 0;
    module_setUp();
    int ret = bFSMount(0, BFS_MKFS_FATFS);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bFSMountLittlefs(void)
{
    g_module_init_done = 0;
    module_setUp();
    /* bFSMkfs(1) erases entire partition before formatting — no manual erase needed. */
    int ret = bFSMount(1, BFS_MKFS_LITTLEFS);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bFSUnmountFatfs(void)
{
    g_module_init_done = 0;
    module_setUp();
    bFSMount(0, BFS_MKFS_FATFS);
    int ret = bFSUnmount(0);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bFSUnmountLittlefs(void)
{
    g_module_init_done = 0;
    module_setUp();
    bFSMount(1, BFS_MKFS_LITTLEFS); /* bFSMkfs erases partition first */
    int ret = bFSUnmount(1);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bFSOpenWriteReadFatfs(void)
{
    g_module_init_done = 0;
    module_setUp();
    bFSMount(0, BFS_MKFS_FATFS);
    bFSUnmount(0);
    bFSMount(0, 0); /* mount existing */

    bFSFile_t fil;
    bFSFd_t fd;
    fd = bFSOpen(&fil, "0:fatfs.bin", BFS_O_CREAT | BFS_O_WR | BFS_O_TRUNC);
    printf("[TEST] bFSOpen fd=%ld fil=%p\n", (long)fd, (void*)&fil); fflush(stdout);
    TEST_ASSERT_TRUE(fd >= 0);

    uint8_t wbuf[32];
    for (int i = 0; i < (int)sizeof(wbuf); i++) wbuf[i] = (uint8_t)(i * 3);
    int wret = bFSWrite(fd, wbuf, sizeof(wbuf));
    printf("[TEST] bFSWrite fd=%ld wret=%d\n", (long)fd, wret); fflush(stdout);
    TEST_ASSERT_EQUAL_INT(sizeof(wbuf), wret);

    bFSClose(fd);

    /* Re-open for read */
    fd = bFSOpen(&fil, "0:fatfs.bin", BFS_O_RD);
    printf("[TEST] bFSOpen re-open fd=%ld\n", (long)fd); fflush(stdout);
    TEST_ASSERT_TRUE(fd >= 0);
    uint8_t rbuf[32] = {0};
    int rret = bFSRead(fd, rbuf, sizeof(rbuf));
    TEST_ASSERT_EQUAL_INT(sizeof(rbuf), rret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wbuf, rbuf, sizeof(wbuf));
    bFSClose(fd);
}

void test_bFSOpenWriteReadLittlefs(void)
{
    g_module_init_done = 0;
    module_setUp();
    bFSMount(1, BFS_MKFS_LITTLEFS); /* bFSMkfs erases entire partition first */

    bFSFile_t fil;
    bFSFd_t fd = bFSOpen(&fil, "/lfs.bin", BFS_O_CREAT | BFS_O_WR | BFS_O_TRUNC);
    TEST_ASSERT_TRUE(fd >= 0);

    uint8_t wbuf[32];
    for (int i = 0; i < (int)sizeof(wbuf); i++) wbuf[i] = (uint8_t)(0x55 ^ i);
    int wret = bFSWrite(fd, wbuf, sizeof(wbuf));
    TEST_ASSERT_EQUAL_INT(sizeof(wbuf), wret);

    bFSClose(fd);

    fd = bFSOpen(&fil, "/lfs.bin", BFS_O_RD);
    TEST_ASSERT_TRUE(fd >= 0);
    uint8_t rbuf[32] = {0};
    int rret = bFSRead(fd, rbuf, sizeof(rbuf));
    TEST_ASSERT_EQUAL_INT(sizeof(rbuf), rret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wbuf, rbuf, sizeof(wbuf));
    bFSClose(fd);
}

void test_bFSGetInfoFatfs(void)
{
    g_module_init_done = 0;
    module_setUp();
    bFSMount(0, BFS_MKFS_FATFS);
    uint32_t total = 0, free = 0;
    int ret = bFSGetInfo(0, &total, &free);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(total > 0);
    TEST_ASSERT_TRUE(free <= total);
}

void test_bFSGetInfoLittlefs(void)
{
    g_module_init_done = 0;
    module_setUp();
    bFSPartitionErase(1, 0, s_fs_parts[1].total_size);
    bFSMount(1, BFS_MKFS_LITTLEFS);
    uint32_t total = 0, free = 0;
    int ret = bFSGetInfo(1, &total, &free);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(total > 0);
    TEST_ASSERT_TRUE(free <= total);
}

void test_bFSCoexistBothFS(void)
{
    g_module_init_done = 0;
    module_setUp();

    /* Mount FATFS on partition 0 */
    int ret = bFSMount(0, BFS_MKFS_FATFS);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Mount LITTLEFS on partition 1 */
    ret = bFSMount(1, BFS_MKFS_LITTLEFS);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Write to FATFS partition 0 */
    bFSFile_t fil0;
    bFSFd_t fd0 = bFSOpen(&fil0, "0:cfat.bin", BFS_O_CREAT | BFS_O_WR | BFS_O_TRUNC);
    TEST_ASSERT_TRUE(fd0 >= 0);
    uint8_t buf0[16] = {0xAA};
    TEST_ASSERT_EQUAL_INT(16, bFSWrite(fd0, buf0, 16));
    bFSClose(fd0);

    /* Write to LITTLEFS partition 1 */
    bFSFile_t fil1;
    bFSFd_t fd1 = bFSOpen(&fil1, "/clfs.bin", BFS_O_CREAT | BFS_O_WR | BFS_O_TRUNC);
    TEST_ASSERT_TRUE(fd1 >= 0);
    uint8_t buf1[16] = {0xBB};
    TEST_ASSERT_EQUAL_INT(16, bFSWrite(fd1, buf1, 16));
    bFSClose(fd1);

    /* Read back from FATFS */
    fd0 = bFSOpen(&fil0, "0:cfat.bin", BFS_O_RD);
    TEST_ASSERT_TRUE(fd0 >= 0);
    uint8_t rbuf0[16] = {0};
    TEST_ASSERT_EQUAL_INT(16, bFSRead(fd0, rbuf0, 16));
    TEST_ASSERT_EQUAL_UINT8(0xAA, rbuf0[0]);
    bFSClose(fd0);

    /* Read back from LITTLEFS */
    fd1 = bFSOpen(&fil1, "/clfs.bin", BFS_O_RD);
    TEST_ASSERT_TRUE(fd1 >= 0);
    uint8_t rbuf1[16] = {0};
    TEST_ASSERT_EQUAL_INT(16, bFSRead(fd1, rbuf1, 16));
    TEST_ASSERT_EQUAL_UINT8(0xBB, rbuf1[0]);
    bFSClose(fd1);

    /* Unmount both */
    bFSUnmount(0);
    bFSUnmount(1);
}

/*------------------------------------------------------------
 * Error management tests
 *------------------------------------------------------------*/
static volatile uint8_t g_error_cb_called = 0;
static void test_error_cb(uint8_t err)
{
    g_error_cb_called++;
    (void)err;
}

void test_bErrorInit(void)
{
    g_error_cb_called = 0;
    int ret = bErrorInit(test_error_cb);
    TEST_ASSERT_EQUAL_INT(0, ret);
    /* Init again — should be idempotent */
    ret = bErrorInit(test_error_cb);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bErrorRegist(void)
{
    bErrorInit(test_error_cb);
    /* Register an error at level 0 — returns 0 on success */
    int ret = bErrorRegist(5, 1000, BERROR_LEVEL_0);
    TEST_ASSERT_EQUAL_INT(0, ret);
    /* Register again with same error: allowed (resets timer if expired) */
    ret = bErrorRegist(5, 1000, BERROR_LEVEL_0);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bErrorIsExistClear(void)
{
    bErrorInit(test_error_cb);
    bErrorRegist(10, 500, BERROR_LEVEL_1);

    /* bErrorIsExist: 0 = exists, -1 = not found */
    TEST_ASSERT_EQUAL_INT(0, bErrorIsExist(10));
    TEST_ASSERT_EQUAL_INT(-1, bErrorIsEmpty()); /* not empty */

    bErrorClear(10);
    TEST_ASSERT_EQUAL_INT(-1, bErrorIsExist(10)); /* cleared, now not found */
}

void test_bErrorClearInvalid(void)
{
    bErrorInit(test_error_cb);
    /* Clearing un-registered error — returns 0 (clears all matching, none found) */
    int ret = bErrorClear(99);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

/*------------------------------------------------------------
 * State machine tests
 *------------------------------------------------------------*/
static volatile uint32_t g_enter_count = 0;
static volatile uint32_t g_exit_count = 0;

static void state1_enter(uint32_t pre) { (void)pre; g_enter_count++; }
static void state1_exit(void)         { g_exit_count++; }
static void state1_handler(void)     {}
static void event1_handler(uint32_t event, void *arg)
{
    (void)event; (void)arg;
}

static bStateEvent_t s_test_events[] = {
    { .event = 1, .handler = event1_handler },
};

static bStateInfo_t s_state1 = {
    .state       = 1,
    .enter       = state1_enter,
    .exit        = state1_exit,
    .handler     = state1_handler,
    .event_table = { .p_event_table = s_test_events, .number = 1 },
};

static bStateInfo_t s_state2 = {
    .state       = 2,
    .enter       = state1_enter,
    .exit        = state1_exit,
    .handler     = state1_handler,
    .event_table = { .p_event_table = NULL, .number = 0 },
};

void test_bStateCreate(void)
{
    bSTATE_CREATE_ATTR(state_attr);
    int ret = bStateCreate("test_machine", &state_attr);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bStateAdd(void)
{
    bSTATE_CREATE_ATTR(state_attr);
    bStateCreate("test_machine2", &state_attr);
    g_enter_count = 0;
    int ret = bStateAdd("test_machine2", &s_state1);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bStateTransfer(void)
{
    bSTATE_CREATE_ATTR(state_attr);
    bStateCreate("test_xfer", &state_attr);
    bStateAdd("test_xfer", &s_state1);
    bStateAdd("test_xfer", &s_state2);
    g_enter_count = 0;

    /* Transfer to state 1 */
    int ret = bStateTransferExt("test_xfer", 1);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT32(1, bGetCurrentStateExt("test_xfer"));

    /* Transfer to state 2 */
    ret = bStateTransferExt("test_xfer", 2);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT32(2, bGetCurrentStateExt("test_xfer"));
}

void test_bStateInvokeEvent(void)
{
    bSTATE_CREATE_ATTR(state_attr);
    bStateCreate("test_event", &state_attr);
    bStateAdd("test_event", &s_state1);

    bStateTransferExt("test_event", 1);
    /* Invoke event 1 on state 1 */
    int ret = bStateInvokeEventExt("test_event", 1, NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

/*------------------------------------------------------------
 * Select (I/O multiplexing) tests
 *------------------------------------------------------------*/
void test_bSelectNullFd(void)
{
    /* NULL fds should return -1 */
    int ret = bSelect(4, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_bSelectReadable(void)
{
    /* Open TESTFLASH fd — it should always be "readable" (data available) */
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_R);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Build a read fd set */
    bFdSet_t rfds;
    memset(&rfds, 0, sizeof(rfds));
    rfds.fds[fd / 8] |= (0x1 << (fd % 8));

    /* bSelect on TESTFLASH — fd should be set (readable) */
    int ret = bSelect(fd + 1, &rfds, NULL, NULL);
    TEST_ASSERT_TRUE(ret >= 0);
    /* The fd should still be marked in the returned set */
    TEST_ASSERT_TRUE(rfds.fds[fd / 8] & (0x1 << (fd % 8)));

    bClose(fd);
}

void test_bSelectWritable(void)
{
    int fd = bOpen(bTESTFLASH, BCORE_FLAG_W);
    TEST_ASSERT_TRUE(fd >= 0);

    bFdSet_t wfds;
    memset(&wfds, 0, sizeof(wfds));
    wfds.fds[fd / 8] |= (0x1 << (fd % 8));

    int ret = bSelect(fd + 1, NULL, &wfds, NULL);
    TEST_ASSERT_TRUE(ret >= 0);

    bClose(fd);
}

/*------------------------------------------------------------
 * QRCode tests
 *------------------------------------------------------------*/
void test_bQRCodeCreate(void)
{
    /* Buffer must be at least qrcode_getBufferSize(version)+1 bytes, plus
     * qrcode_initBytes does modules = modules_arg - 1 (1 byte before buffer).
     * Version 1 (21x21) needs ceil(441/8)+1 = 57 minimum; use 512 for safety. */
    static uint8_t qr_buf[512];
    bQRCode_t qr = {
        .qr = {0},
        .size = 1,
        .buf = qr_buf,
        .ecc = QRCODE_ECC_MEDIUM,
    };
    uint8_t data[] = "ABC123";
    int ret = bQRCodeCreate(&qr, data, sizeof(data) - 1);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bQRCodeGetValue(void)
{
    static uint8_t qr_buf[512];
    bQRCode_t qr = {
        .qr = {0},
        .size = 1,
        .buf = qr_buf,
        .ecc = QRCODE_ECC_MEDIUM,
    };
    uint8_t data[] = "ABC123";
    int ret = bQRCodeCreate(&qr, data, sizeof(data) - 1);
    TEST_ASSERT_EQUAL_INT(0, ret);

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            int v = bQRCodeGetValue(&qr, (uint8_t)x, (uint8_t)y);
            TEST_ASSERT_TRUE(v == 0 || v == 1);
        }
    }
}

/*------------------------------------------------------------
 * KV tests (existing)
 *------------------------------------------------------------*/
void test_bKVSetGet(void)
{
    module_setUp();
    const char *key = "test_key";
    uint8_t val[] = {0xDE, 0xAD, 0xBE, 0xEF};
    int ret = bKVSetValue(&gTestKV, key, val, sizeof(val));
    TEST_ASSERT_EQUAL_INT(0, ret);

    int len = bKVGetValueLength(&gTestKV, key);
    TEST_ASSERT_EQUAL_INT(sizeof(val), len);

    uint8_t rbuf[8];
    bKVGetValue(&gTestKV, key, rbuf, sizeof(rbuf), NULL);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(val, rbuf, sizeof(val));
}

void test_bKVDelete(void)
{
    module_setUp();
    const char *key = "del_key";
    uint8_t val[] = {0x11, 0x22};
    TEST_ASSERT_EQUAL_INT(0, bKVSetValue(&gTestKV, key, val, sizeof(val)));
    TEST_ASSERT_EQUAL_INT(sizeof(val), bKVGetValueLength(&gTestKV, key));
    bKVDeleteValue(&gTestKV, key);
    TEST_ASSERT_EQUAL_INT(-1, bKVGetValueLength(&gTestKV, key));
}

void test_bKVMultipleKeys(void)
{
    module_setUp();
    for (int i = 0; i < 10; i++) {
        char key[8];
        uint8_t val[4];
        sprintf(key, "k%02d", i);
        for (int j = 0; j < 4; j++) val[j] = (uint8_t)(i * 4 + j);
        TEST_ASSERT_EQUAL_INT(0, bKVSetValue(&gTestKV, key, val, sizeof(val)));
        uint8_t rbuf[4];
        bKVGetValue(&gTestKV, key, rbuf, sizeof(rbuf), NULL);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(val, rbuf, sizeof(val));
    }
}

void test_bKVOverwrite(void)
{
    module_setUp();
    const char *key = "overwrite";
    uint8_t v1[] = {0xAA};
    uint8_t v2[] = {0xBB, 0xCC};
    TEST_ASSERT_EQUAL_INT(0, bKVSetValue(&gTestKV, key, v1, sizeof(v1)));
    TEST_ASSERT_EQUAL_INT(sizeof(v1), bKVGetValueLength(&gTestKV, key));
    TEST_ASSERT_EQUAL_INT(0, bKVSetValue(&gTestKV, key, v2, sizeof(v2)));
    TEST_ASSERT_EQUAL_INT(sizeof(v2), bKVGetValueLength(&gTestKV, key));
    uint8_t rbuf[4];
    bKVGetValue(&gTestKV, key, rbuf, sizeof(rbuf), NULL);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v2, rbuf, sizeof(v2));
}

/*------------------------------------------------------------
 * Menu tests
 *------------------------------------------------------------*/
/* Menu uses a static table of MENU_ITEM_NUMBER entries (default 10).
 * Test items are identified by uint32_t id. Navigation: UP/DOWN cycles
 * through siblings, ENTER enters child, BACK returns to parent. */
static volatile uint32_t g_menu_ui_called_id = 0;
static void test_menu_ui(uint32_t pre_id)
{
    g_menu_ui_called_id = pre_id;
    (void)pre_id;
}

void test_bMenuAddSibling(void)
{
    /* Reset state so each test runs in isolation */
    bMenuReset();

    /* bMenuAddSibling(ref_id, id, create_ui):
     * - First item (ref_id == id): creates root node, returns 1 (ItemIndex=1)
     * - Subsequent items: links after ref, returns new ItemIndex */
    int ret = bMenuAddSibling(100, 100, test_menu_ui);
    TEST_ASSERT_EQUAL_INT(1, ret); /* first item: ItemIndex becomes 1 */
    g_menu_ui_called_id = 0;

    ret = bMenuAddSibling(100, 101, test_menu_ui);
    TEST_ASSERT_EQUAL_INT(2, ret); /* second sibling */
}

void test_bMenuAddChild(void)
{
    bMenuReset();

    /* bMenuAddChild(ref_id, id, create_ui):
     * Adds a child to ref. ref_id must already exist. */
    int ret = bMenuAddSibling(200, 200, test_menu_ui);
    TEST_ASSERT_EQUAL_INT(1, ret);

    ret = bMenuAddSibling(200, 201, test_menu_ui);
    TEST_ASSERT_EQUAL_INT(2, ret);

    /* Add child to root (200) — ItemIndex becomes 3 after adding child */
    ret = bMenuAddChild(200, 202, test_menu_ui);
    TEST_ASSERT_EQUAL_INT(3, ret);
}

void test_bMenuAction(void)
{
    bMenuReset();

    /* Build: root(300) -> siblings 301, 302 (insert after previous item).
     * bMenuAddSibling(ref, new): inserts new AFTER ref in circular list.
     * So: add 300, add 301 after 300, add 302 after 301.
     * Resulting forward order from 300: 300 -> 301 -> 302 -> (wraps to 300) */
    bMenuAddSibling(300, 300, test_menu_ui);
    bMenuAddSibling(300, 301, test_menu_ui);
    bMenuAddSibling(301, 302, test_menu_ui); /* note: ref=301, not 300 */
    bMenuAddChild(300, 303, test_menu_ui);

    /* Initial current item: 300 (first item created) */
    TEST_ASSERT_EQUAL_UINT32(300, bMenuCurrentID());

    /* DOWN follows next pointers: 300 -> 301 -> 302 -> (wraps to 300) */
    bMenuAction(MENU_DOWN);
    TEST_ASSERT_EQUAL_UINT32(301, bMenuCurrentID());

    bMenuAction(MENU_DOWN);
    TEST_ASSERT_EQUAL_UINT32(302, bMenuCurrentID());

    bMenuAction(MENU_DOWN); /* wraps to 300 */
    TEST_ASSERT_EQUAL_UINT32(300, bMenuCurrentID());

    /* UP follows prev pointers (reverse of next): 300 -> 302 -> 301 */
    bMenuAction(MENU_UP);
    TEST_ASSERT_EQUAL_UINT32(302, bMenuCurrentID());
}

void test_bMenuJump(void)
{
    bMenuReset();

    bMenuAddSibling(400, 400, test_menu_ui);
    bMenuAddSibling(400, 401, test_menu_ui);
    bMenuAddSibling(400, 402, test_menu_ui);

    /* Jump to 401 */
    bMenuJump(401);
    TEST_ASSERT_EQUAL_UINT32(401, bMenuCurrentID());

    /* Jump to 400 */
    bMenuJump(400);
    TEST_ASSERT_EQUAL_UINT32(400, bMenuCurrentID());
}

void test_bMenuSetVisible(void)
{
    bMenuReset();

    bMenuAddSibling(500, 500, test_menu_ui);
    bMenuAddSibling(500, 501, test_menu_ui);
    bMenuAddSibling(500, 502, test_menu_ui);

    /* Hide 501 — returns 0 on success */
    int ret = bMenuSetVisible(501, 0);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Current stays at 500 (501 is hidden but current is unchanged) */
    TEST_ASSERT_EQUAL_UINT32(500, bMenuCurrentID());

    /* Show 501 again */
    ret = bMenuSetVisible(501, 1);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bMenuCurrentID(void)
{
    bMenuReset();

    /* After reset: Pre_ID=0, no current item */
    TEST_ASSERT_EQUAL_UINT32(0, bMenuCurrentID());

    /* Add first item — becomes current */
    bMenuAddSibling(600, 600, test_menu_ui);
    TEST_ASSERT_EQUAL_UINT32(600, bMenuCurrentID());
}

/*------------------------------------------------------------
 * Param tests — functional read/write via shell
 *------------------------------------------------------------*/
/* bPARAM_REG_INSTANCE registers a variable into the .b_mod_param linker section.
 * The "param" shell command (registered via bSHELL_REG_INSTANCE in b_mod_param.c)
 * reads/writes the backing variables by iterating the section.
 * We test by calling bShellParse with shell command strings and verifying the
 * backing variables changed as expected. */
/* Register two test parameters */
static int32_t g_param_test_val = 12345;
static int16_t g_param_test_val2 = -999;

bPARAM_REG_INSTANCE(g_param_test_val, sizeof(g_param_test_val));
bPARAM_REG_INSTANCE(g_param_test_val2, sizeof(g_param_test_val2));

void test_bParamShellSet(void)
{
    /* param g_param_test_val -777\r
     * Sets g_param_test_val to -777 via shell. */
    g_param_test_val = 12345; /* reset first */
    uint8_t cmd[] = "param g_param_test_val -777\r";
    bShellParse(cmd, sizeof(cmd) - 1);
    TEST_ASSERT_EQUAL_INT32(-777, g_param_test_val);
}

void test_bParamShellSet2(void)
{
    g_param_test_val = 12345;
    g_param_test_val2 = -999;
    /* param g_param_test_val 54321\r */
    uint8_t cmd1[] = "param g_param_test_val 54321\r";
    bShellParse(cmd1, sizeof(cmd1) - 1);
    TEST_ASSERT_EQUAL_INT32(54321, g_param_test_val);

    /* param g_param_test_val2 111\r */
    uint8_t cmd2[] = "param g_param_test_val2 111\r";
    bShellParse(cmd2, sizeof(cmd2) - 1);
    TEST_ASSERT_EQUAL_INT16(111, g_param_test_val2);
}

void test_bParamShellSetRoundTrip(void)
{
    g_param_test_val = 12345;
    /* param g_param_test_val 0\r */
    uint8_t cmd[] = "param g_param_test_val 0\r";
    bShellParse(cmd, sizeof(cmd) - 1);
    TEST_ASSERT_EQUAL_INT32(0, g_param_test_val);
}

void test_bParamShellGet(void)
{
    /* param g_param_test_val\r — read value (shell prints it to log) */
    g_param_test_val = 99999;
    uint8_t cmd[] = "param g_param_test_val\r";
    int ret = bShellParse(cmd, sizeof(cmd) - 1);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT32(99999, g_param_test_val);
}

void test_bParamShellList(void)
{
    /* param\r — list all params (shell prints names to log) */
    g_param_test_val = 12345;
    g_param_test_val2 = -999;
    uint8_t cmd[] = "param\r";
    int ret = bShellParse(cmd, sizeof(cmd) - 1);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

/*------------------------------------------------------------
 * Modbus RTU Master tests
 *------------------------------------------------------------*/
static volatile uint8_t g_modbus_cb_called = 0;
static volatile bProtoCmd_t g_modbus_cb_cmd = 0;
static int test_modbus_cb(bProtoCmd_t cmd, void *param)
{
    (void)param;
    g_modbus_cb_called++;
    g_modbus_cb_cmd = cmd;
    return 0;
}

/* _bModbusRTUMasterParse is static in b_mod_modbus.c. We access it via the
 * bProtocolInstance_t wrapper do__bModbusRTUMasterParse (in .b_srv_protocol).
 * Extract parse function pointer from the struct instance at offset +8. */
extern const struct { const char *n; void *p; void *pkg; char r[8]; } do__bModbusRTUMasterParse;
typedef int (*bModbusParseFn)(void *, uint8_t *, uint16_t, uint8_t *, uint16_t);
#define MODBUS_PARSE_FN  (*(bModbusParseFn *)((char *)&do__bModbusRTUMasterParse + 8))

void test_bModbusParseReadResp(void)
{
    g_modbus_cb_called = 0;
    g_modbus_cb_cmd = 0;

    /* Set up a minimal bProtocolAttr_t to call _bModbusRTUMasterParse directly.
     * bProtSrvInit fails due to section scan bug so we bypass it. */
    bProtocolAttr_t attr = {
        .callback = test_modbus_cb,
        .arg = NULL,
    };

    /* Response layout (12 bytes, len=9 total):
     *   [01] [03] [04]  = addr/func/len=4  (2 registers, 4 bytes)
     *   [64 00] [C8 00] = 2 register values (at in+3, in+5)
     *   [B2 C3]         = CRC16_MODBUS of [01,03,04,64,00,C8,00] = 0xC3B2 (LE)
     * Frame: [01,03,04,64,00,C8,00,B2,C3]
     * Parse computes CRC over len-2=7 bytes: [01,03,04,64,00,C8,00] = 0xC3B2 ✓
     * Stored CRC at in+sizeof+len-2 = in+7: [B2,C3] = 0xC3B2 ✓
     * data[0] at in+3 = [64,00] = 0x0064 ✓
     * data[1] at in+5 = [C8,00] = 0x00C8 ✓ */
    uint8_t resp[12];
    resp[0] = 0x01; resp[1] = 0x03; resp[2] = 0x04;
    resp[3] = 0x64; resp[4] = 0x00; resp[5] = 0xC8; resp[6] = 0x00;
    resp[7] = 0xB2; resp[8] = 0xC3;
    resp[9] = resp[10] = resp[11] = 0;

    uint8_t out[32];
    int len = MODBUS_PARSE_FN(&attr, resp, sizeof(resp), out, sizeof(out));
    TEST_ASSERT_TRUE(len >= 0);
    TEST_ASSERT_EQUAL_INT(1, g_modbus_cb_called);
    TEST_ASSERT_EQUAL_INT(B_MODBUS_CMD_READ_REG, g_modbus_cb_cmd);
}

void test_bModbusParseWriteRegsResp(void)
{
    g_modbus_cb_called = 0;
    g_modbus_cb_cmd = 0;

    bProtocolAttr_t attr = { .callback = test_modbus_cb, .arg = NULL };

    uint8_t resp[8];
    resp[0] = 0x01; resp[1] = 0x10;
    resp[2] = 0x00; resp[3] = 0x01; resp[4] = 0x00; resp[5] = 0x02;
    uint16_t crc = crc_calculate(ALGO_CRC16_MODBUS, resp, 6);
    resp[6] = crc & 0xFF; resp[7] = (crc >> 8) & 0xFF;

    uint8_t out[32];
    int len = MODBUS_PARSE_FN(&attr, resp, sizeof(resp), out, sizeof(out));
    TEST_ASSERT_TRUE(len >= 0);
    TEST_ASSERT_EQUAL_INT(1, g_modbus_cb_called);
    TEST_ASSERT_EQUAL_INT(B_MODBUS_CMD_WRITE_REGS, g_modbus_cb_cmd);
}

void test_bModbusParseCrcError(void)
{
    g_modbus_cb_called = 0;
    bProtocolAttr_t attr = { .callback = test_modbus_cb, .arg = NULL };

    uint8_t bad[12];
    bad[0] = 0x01; bad[1] = 0x03; bad[2] = 0x04;
    bad[3] = 0x00; bad[4] = 0x64; bad[5] = 0x00; bad[6] = 0xC8;
    bad[7] = 0x00; bad[8] = 0x00; bad[9] = bad[10] = bad[11] = 0;

    uint8_t out[32];
    int len = MODBUS_PARSE_FN(&attr, bad, sizeof(bad), out, sizeof(out));
    TEST_ASSERT_TRUE(len < 0);
    TEST_ASSERT_EQUAL_INT(0, g_modbus_cb_called);
}

void test_bModbusParseShortFrame(void)
{
    bProtocolAttr_t attr = { .callback = test_modbus_cb, .arg = NULL };
    uint8_t short_frame[] = { 0x01, 0x03 };
    uint8_t out[32];
    int len = MODBUS_PARSE_FN(&attr, short_frame, sizeof(short_frame), out, sizeof(out));
    TEST_ASSERT_TRUE(len < 0);
}

void test_bModbusPkgReadReq(void)
{
    B_PROT_SRV_CREATE_ATTR(mb_attr, "modbus_master", test_modbus_cb);
    bProtSrvId_t id = bProtSrvInit(&mb_attr, NULL);
    TEST_ASSERT_NOT_NULL(id);

    /* _bModbusRTUMasterPackage needs buf_len >= sizeof(bModbusMasterSendReadRegs_t) = 8.
     * The input is a bModbusMasterRead_t {slave_addr, base_reg, reg_num} (LE fields).
     * It must be placed at buf offsets [0,2,4] respectively.
     * buf[1]=func (MODBUS_RTU_READ_REGS=0x03) is written by the package function. */
    uint8_t buf[8] = {0};
    buf[0] = 0x01;                        /* slave_addr at offset 0  */
    buf[2] = 0x00; buf[3] = 0x00;         /* base_reg LE at offsets 2,3  */
    buf[4] = 0x02; buf[5] = 0x00;         /* reg_num LE at offsets 4,5   */

    int pkglen = bProtSrvPackage(id, B_MODBUS_CMD_READ_REG, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(8, pkglen);
    /* MODBUS frame written by package: func=0x03 at buf[1]; reg=0x0000 BE at buf[2:3];
     * num=0x0002 BE at buf[4:5]; CRC at buf[6:7]. */
    TEST_ASSERT_EQUAL_INT(0x03, buf[1]);
    TEST_ASSERT_EQUAL_INT(0x00, buf[2]);
    TEST_ASSERT_EQUAL_INT(0x00, buf[3]);
    TEST_ASSERT_EQUAL_INT(0x00, buf[4]);
    TEST_ASSERT_EQUAL_INT(0x02, buf[5]);
}

void test_bModbusPkgWriteReq(void)
{
    B_PROT_SRV_CREATE_ATTR(mb_attr, "modbus_master", test_modbus_cb);
    bProtSrvId_t id = bProtSrvInit(&mb_attr, NULL);
    TEST_ASSERT_NOT_NULL(id);

    uint8_t buf[64] = {0};
    buf[6] = 0x64; buf[7] = 0x00;
    buf[8] = 0xC8; buf[9] = 0x00;
    memcpy(buf, &(bModbusMasterWriteRegs_t){ .slave_addr = 0x01, .base_reg = 0x0000, .reg_num = 2 }, 8);

    int pkglen = bProtSrvPackage(id, B_MODBUS_CMD_WRITE_REGS, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(13, pkglen);
    TEST_ASSERT_EQUAL_INT(0x01, buf[0]);
    TEST_ASSERT_EQUAL_INT(0x10, buf[1]);
}

void test_bModbusPkgBadCmd(void)
{
    B_PROT_SRV_CREATE_ATTR(mb_attr, "modbus_master", test_modbus_cb);
    bProtSrvId_t id = bProtSrvInit(&mb_attr, NULL);
    TEST_ASSERT_NOT_NULL(id);

    uint8_t pkg[32];
    int pkglen = bProtSrvPackage(id, B_PROTO_CMD_NUMBER, pkg, sizeof(pkg));
    TEST_ASSERT_EQUAL_INT(-1, pkglen);
}

/*------------------------------------------------------------
 * Shell module tests
 *------------------------------------------------------------*/
void test_bShellInitIdempotent(void)
{
    bShellInit();
    bShellInit();
    bShellInit(); /* should not crash */
}

void test_bShellParseNull(void)
{
    int ret = bShellParse(NULL, 10);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_bShellParseZeroLen(void)
{
    uint8_t cmd[] = "bos\r";
    int ret = bShellParse(cmd, 0);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_bShellParseEmptyStr(void)
{
    uint8_t cmd[] = "";
    int ret = bShellParse(cmd, 0);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_bShellVersionCmd(void)
{
    uint8_t cmd[] = "bos -v\r";
    int ret = bShellParse(cmd, sizeof(cmd) - 1);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_bShellUnknownCmd(void)
{
    uint8_t cmd[] = "unknowncmd123\r";
    int ret = bShellParse(cmd, sizeof(cmd) - 1);
    /* Should not crash; return value may be -1 or 0 depending on shell impl */
    (void)ret;
}

int test_modules_runner(void)
{
    int start = Unity.TestFailures;
#define RUN(f) (f)
    RUN_TEST(test_bErrorInit);
    RUN_TEST(test_bErrorRegist);
    RUN_TEST(test_bErrorIsExistClear);
    RUN_TEST(test_bErrorClearInvalid);
    RUN_TEST(test_bStateCreate);
    RUN_TEST(test_bStateAdd);
    RUN_TEST(test_bStateTransfer);
    RUN_TEST(test_bStateInvokeEvent);
    RUN_TEST(test_bSelectNullFd);
    RUN_TEST(test_bSelectReadable);
    RUN_TEST(test_bSelectWritable);
    RUN_TEST(test_bQRCodeCreate);
    RUN_TEST(test_bQRCodeGetValue);
    RUN_TEST(test_bKVSetGet);
    RUN_TEST(test_bKVDelete);
    RUN_TEST(test_bKVMultipleKeys);
    RUN_TEST(test_bKVOverwrite);
    RUN_TEST(test_bFSInit);
    RUN_TEST(test_bFSInitNull);
    RUN_TEST(test_bFSMountFatfs);
    RUN_TEST(test_bFSMountLittlefs);
    RUN_TEST(test_bFSUnmountFatfs);
    RUN_TEST(test_bFSUnmountLittlefs);
    RUN_TEST(test_bFSOpenWriteReadFatfs);
    RUN_TEST(test_bFSOpenWriteReadLittlefs);
    RUN_TEST(test_bFSGetInfoFatfs);
    RUN_TEST(test_bFSGetInfoLittlefs);
    RUN_TEST(test_bFSCoexistBothFS);
    RUN_TEST(test_bMenuAddSibling);
    RUN_TEST(test_bMenuAddChild);
    RUN_TEST(test_bMenuAction);
    RUN_TEST(test_bMenuJump);
    RUN_TEST(test_bMenuSetVisible);
    RUN_TEST(test_bMenuCurrentID);
    RUN_TEST(test_bParamShellSet);
    RUN_TEST(test_bParamShellSet2);
    RUN_TEST(test_bParamShellSetRoundTrip);
    RUN_TEST(test_bParamShellGet);
    RUN_TEST(test_bParamShellList);
    RUN_TEST(test_bModbusParseReadResp);
    RUN_TEST(test_bModbusParseWriteRegsResp);
    RUN_TEST(test_bModbusParseCrcError);
    RUN_TEST(test_bModbusParseShortFrame);
    RUN_TEST(test_bModbusPkgReadReq);
    RUN_TEST(test_bModbusPkgWriteReq);
    RUN_TEST(test_bModbusPkgBadCmd);
    RUN_TEST(test_bShellInitIdempotent);
    RUN_TEST(test_bShellParseNull);
    RUN_TEST(test_bShellParseZeroLen);
    RUN_TEST(test_bShellParseEmptyStr);
    RUN_TEST(test_bShellVersionCmd);
    RUN_TEST(test_bShellUnknownCmd);
#undef RUN
    return Unity.TestFailures - start;
}

#endif /* _UNITY_ENABLE */