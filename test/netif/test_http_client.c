/**
 * \file test_http_client.c
 * \brief HTTP client regression tests for test/netif (host Linux + testmac)
 * \version 0.1
 * \date 2026-08-17
 *
 * Tests the fixes in bos/services/b_srv_http.c:
 *   - CRIT-HTTP-2: bHttpFd_t must stay valid after a request completes; task
 *     must not self-destruct in DEINIT state.
 *   - CRIT-HTTP-3: state must always return to B_HTTP_STA_DEINIT after any
 *     request cycle (normal, conn-fail, server-reset), so the next
 *     bHttpRequest() does not return "client busy".
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "b_os.h"
#include "services/inc/b_srv_http.h"

/* Mock HTTP server listens on loopback. testmac uses real Linux sockets,
   so a POSIX-thread server works end-to-end with the BabyOS HTTP client. */
#define TEST_HTTP_PORT  18080
#define TEST_HTTP_PORT2 18081

/* Request/response sizes kept small so the test is fast on PC and MCU. */
#define TEST_BODY_LEN   12

/* Test control ------------------------------------------------------------- */
typedef enum
{
    MOCK_IDLE = 0,
    MOCK_NORMAL,        /* 200 OK with small body */
    MOCK_CLOSE_FAST,    /* accept, read a few bytes, close (reset) */
    MOCK_PARTIAL,       /* send incomplete headers then close */
} mock_scenario_t;

static volatile mock_scenario_t s_mock_scenario = MOCK_IDLE;
static volatile int             s_mock_running  = 0;
static volatile int             s_mock_sock     = -1;

/* Callback counters -------------------------------------------------------- */
typedef struct
{
    volatile uint8_t connected;
    volatile uint8_t recv_data;
    volatile uint8_t conn_fail;
    volatile uint8_t error;
    volatile uint8_t destroy;
    volatile int     recv_len;
    volatile int     done;
} test_http_cb_state_t;

static test_http_cb_state_t s_cb_state;

/* Test runner state -------------------------------------------------------- */
typedef enum
{
    TST_IDLE = 0,
    TST_NORMAL,
    TST_SEQ_SECOND,
    TST_CONN_FAIL,
    TST_RESET_RECOVER,
    TST_POST_BODY,
    TST_DONE,
} test_http_stage_t;

static volatile test_http_stage_t s_stage = TST_IDLE;
static volatile int               s_fail  = 0;
static bTaskId_t                  s_test_task_id = NULL;
volatile int                      test_http_client_done = 0;

/* Helper: wait for socket readable with timeout (ms). */
static int _sock_wait_readable(int sock, int timeout_ms)
{
    fd_set         rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(sock + 1, &rfds, NULL, NULL, &tv);
}

/* Minimal path extractor: "GET /path HTTP/1.1" -> copies /path into buf. */
static int _extract_path(const char *req, int req_len, char *buf, int buf_size)
{
    const char *p1 = memchr(req, ' ', req_len);
    if (p1 == NULL) return -1;
    p1++;
    const char *p2 = memchr(p1, ' ', req_len - (int)(p1 - req));
    if (p2 == NULL) p2 = req + req_len;
    int len = (int)(p2 - p1);
    if (len <= 0 || len >= buf_size) return -1;
    memcpy(buf, p1, len);
    buf[len] = '\0';
    return 0;
}

/* Mock HTTP server thread -------------------------------------------------- */
static void *_test_http_server_thread(void *arg)
{
    int port = *(int *)arg;
    int server_sock, client_sock;
    struct sockaddr_in addr;
    int opt = 1;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        b_log_e("[TEST-HTTP] server socket() failed\r\n");
        return NULL;
    }
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        b_log_e("[TEST-HTTP] server bind() failed\r\n");
        close(server_sock);
        return NULL;
    }
    if (listen(server_sock, 2) < 0)
    {
        b_log_e("[TEST-HTTP] server listen() failed\r\n");
        close(server_sock);
        return NULL;
    }

    s_mock_sock    = server_sock;
    s_mock_running = 1;
    b_log("[TEST-HTTP] mock server ready on 127.0.0.1:%d\r\n", port);

    while (s_mock_running)
    {
        struct sockaddr_in client_addr;
        socklen_t          addrlen = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addrlen);
        if (client_sock < 0)
        {
            if (errno == EINTR) continue;
            break;
        }

        mock_scenario_t scenario = s_mock_scenario;

        if (scenario == MOCK_CLOSE_FAST)
        {
            /* Read a little so the client sees a connection, then RST. */
            char tmp[32];
            _sock_wait_readable(client_sock, 200);
            recv(client_sock, tmp, sizeof(tmp), 0);
            close(client_sock);
            continue;
        }

        /* Read full request (enough for our tiny tests). */
        char req[256];
        int  req_len = 0;
        int  rd;
        while (req_len < (int)sizeof(req) - 1)
        {
            if (_sock_wait_readable(client_sock, 500) <= 0) break;
            rd = recv(client_sock, req + req_len, sizeof(req) - 1 - req_len, 0);
            if (rd <= 0) break;
            req_len += rd;
            req[req_len] = '\0';
            if (strstr(req, "\r\n\r\n") != NULL) break;
        }

        char path[64];
        _extract_path(req, req_len, path, sizeof(path));

        if (scenario == MOCK_PARTIAL)
        {
            const char *partial = "HTTP/1.1 200 OK\r\nContent-Length: 999\r\n";
            send(client_sock, partial, strlen(partial), 0);
            close(client_sock);
            continue;
        }

        /* Default: MOCK_NORMAL */
        const char *body = NULL;
        if (strcmp(path, "/test1") == 0)
        {
            body = "Hello World";
        }
        else if (strcmp(path, "/test2a") == 0)
        {
            body = "resp-a";
        }
        else if (strcmp(path, "/test2b") == 0)
        {
            body = "resp-b";
        }
        else if (strcmp(path, "/post") == 0)
        {
            body = "posted";
        }
        else
        {
            body = "ok";
        }

        char resp[128];
        snprintf(resp, sizeof(resp),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: close\r\n"
                 "\r\n%s",
                 (int)strlen(body), body);
        send(client_sock, resp, strlen(resp), 0);
        close(client_sock);
    }

    close(server_sock);
    s_mock_sock    = -1;
    s_mock_running = 0;
    return NULL;
}

/* HTTP client callback ----------------------------------------------------- */
static void _test_http_cb(bHttpEvent_t event, void *param, void *user_data)
{
    (void)user_data;
    switch (event)
    {
        case B_HTTP_EVENT_CONNECTED:
            s_cb_state.connected++;
            break;
        case B_HTTP_EVENT_RECV_DATA:
        {
            s_cb_state.recv_data++;
            bHttpRecvData_t *dat = (bHttpRecvData_t *)param;
            if (dat != NULL)
            {
                s_cb_state.recv_len = dat->len;
            }
            /* In this HTTP client implementation, RECV_DATA is emitted after
               the full response has been parsed; the task then restarts. */
            s_cb_state.done++;
            break;
        }
        case B_HTTP_EVENT_CONN_FAIL:
            s_cb_state.conn_fail++;
            s_cb_state.done++;
            break;
        case B_HTTP_EVENT_ERROR:
        case B_HTTP_EVENT_SSL_FAIL:
        case B_HTTP_EVENT_RECV_TIMEOUT:
            s_cb_state.error++;
            s_cb_state.done++;
            break;
        case B_HTTP_EVENT_DESTROY:
            s_cb_state.destroy++;
            s_cb_state.done++;
            break;
        default:
            break;
    }
}

static void _test_reset_cb_state(void)
{
    memset((void *)&s_cb_state, 0, sizeof(s_cb_state));
}

/* Test result logging ------------------------------------------------------ */
static void _test_pass(const char *name)
{
    b_log("[TEST-HTTP] PASS: %s\r\n", name);
}

static void _test_fail(const char *name, const char *reason)
{
    s_fail++;
    b_log_e("[TEST-HTTP] FAIL: %s (%s)\r\n", name, reason);
}

/* Test runner task --------------------------------------------------------- */
PT_THREAD(bHttpClientTestTask)(struct pt *pt, void *arg)
{
    static bHttpFd_t httpfd;
    static int       ret;
    static int       mem_before;
    static int       mem_after;

    (void)arg;
    B_TASK_INIT_BEGIN();
    B_TASK_INIT_END();

    PT_BEGIN(pt);

    /* Wait for mock server to bind. */
    PT_WAIT_UNTIL(pt, s_mock_running, 3000);
    if (!s_mock_running)
    {
        _test_fail("server startup", "mock server not running");
        PT_EXIT(pt);
    }
    bTaskDelayMs(pt, 100); /* extra settle time */

    mem_before = bGetFreeSize();
    b_log("[TEST-HTTP] heap before: %d\r\n", mem_before);

    /*=========================================================================
     * Test 1: Normal GET request
     *========================================================================*/
    s_stage = TST_NORMAL;
    _test_reset_cb_state();
    httpfd = bHttpInit(_test_http_cb, NULL);
    if (httpfd <= 0)
    {
        _test_fail("normal GET", "bHttpInit failed");
        PT_EXIT(pt);
    }

    ret = bHttpRequest(httpfd, B_HTTP_GET, "http://127.0.0.1:18080/test1", NULL, NULL);
    if (ret != 0)
    {
        _test_fail("normal GET", "bHttpRequest rejected");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }

    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done)
    {
        _test_fail("normal GET", "timeout waiting for completion");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }
    if (s_cb_state.connected == 0)
    {
        _test_fail("normal GET", "no CONNECTED event");
    }
    else if (s_cb_state.recv_data == 0)
    {
        _test_fail("normal GET", "no RECV_DATA event");
    }
    else if (s_cb_state.recv_len <= 0)
    {
        _test_fail("normal GET", "empty response");
    }
    else
    {
        _test_pass("normal GET");
    }

    /*=========================================================================
     * Test 2: Sequential requests without DeInit (regression for "client busy")
     *========================================================================*/
    s_stage = TST_SEQ_SECOND;
    _test_reset_cb_state();

    ret = bHttpRequest(httpfd, B_HTTP_GET, "http://127.0.0.1:18080/test2a", NULL, NULL);
    if (ret != 0)
    {
        _test_fail("sequential req-a", "bHttpRequest rejected");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }

    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done)
    {
        _test_fail("sequential req-a", "timeout");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }

    /* Immediately fire the second request. This is the regression check:
       before the fix the client task had freed itself or left state != DEINIT,
       causing bHttpRequest to return -2 "client busy". */
    _test_reset_cb_state();
    s_stage = TST_SEQ_SECOND;
    ret = bHttpRequest(httpfd, B_HTTP_GET, "http://127.0.0.1:18080/test2b", NULL, NULL);
    if (ret != 0)
    {
        _test_fail("sequential req-b", "bHttpRequest rejected (client busy?)");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }

    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done)
    {
        _test_fail("sequential req-b", "timeout");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }
    if (s_cb_state.recv_data == 0)
    {
        _test_fail("sequential req-b", "no RECV_DATA");
    }
    else
    {
        _test_pass("sequential requests without DeInit");
    }

    /*=========================================================================
     * Test 3: Connection refused, then recovery
     *========================================================================*/
    s_stage = TST_CONN_FAIL;
    _test_reset_cb_state();

    /* Port 18081 has no listener. */
    ret = bHttpRequest(httpfd, B_HTTP_GET, "http://127.0.0.1:18081/noserver", NULL, NULL);
    if (ret != 0)
    {
        _test_fail("conn refused", "bHttpRequest rejected");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }

    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done)
    {
        _test_fail("conn refused", "timeout");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }
    if (s_cb_state.conn_fail == 0 && s_cb_state.error == 0)
    {
        _test_fail("conn refused", "expected CONN_FAIL/ERROR event");
    }
    else
    {
        _test_pass("connection refused handled");
    }

    /* Now prove state returned to DEINIT by issuing a working request. */
    _test_reset_cb_state();
    ret = bHttpRequest(httpfd, B_HTTP_GET, "http://127.0.0.1:18080/test1", NULL, NULL);
    if (ret != 0)
    {
        _test_fail("recover after conn refused", "bHttpRequest rejected (stuck busy)");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }
    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done || s_cb_state.recv_data == 0)
    {
        _test_fail("recover after conn refused", "did not receive response");
    }
    else
    {
        _test_pass("recover after connection refused");
    }

    /*=========================================================================
     * Test 4: Server resets connection immediately after accept
     *========================================================================*/
    s_stage = TST_RESET_RECOVER;
    _test_reset_cb_state();
    s_mock_scenario = MOCK_CLOSE_FAST;

    ret = bHttpRequest(httpfd, B_HTTP_GET, "http://127.0.0.1:18080/close", NULL, NULL);
    if (ret != 0)
    {
        _test_fail("server reset", "bHttpRequest rejected");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }

    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done)
    {
        _test_fail("server reset", "timeout");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }
    /* After reset we expect either CONN_FAIL or ERROR. */
    if (s_cb_state.conn_fail == 0 && s_cb_state.error == 0)
    {
        _test_fail("server reset", "expected failure event");
    }
    else
    {
        _test_pass("server reset handled");
    }

    /* And must be reusable. */
    s_mock_scenario = MOCK_NORMAL;
    _test_reset_cb_state();
    ret = bHttpRequest(httpfd, B_HTTP_GET, "http://127.0.0.1:18080/test1", NULL, NULL);
    if (ret != 0)
    {
        _test_fail("recover after reset", "bHttpRequest rejected (stuck busy)");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }
    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done || s_cb_state.recv_data == 0)
    {
        _test_fail("recover after reset", "did not receive response");
    }
    else
    {
        _test_pass("recover after server reset");
    }

    /*=========================================================================
     * Test 5: POST with body (sanity for request builder + body path)
     *========================================================================*/
    s_stage = TST_POST_BODY;
    _test_reset_cb_state();
    s_mock_scenario = MOCK_NORMAL;

    ret = bHttpRequest(httpfd, B_HTTP_POST, "http://127.0.0.1:18080/post",
                       "X-Test: 1\r\n", "body-data");
    if (ret != 0)
    {
        _test_fail("POST with body", "bHttpRequest rejected");
        bHttpDeInit(httpfd);
        PT_EXIT(pt);
    }
    PT_WAIT_UNTIL(pt, s_cb_state.done, 10000);
    if (!s_cb_state.done || s_cb_state.recv_data == 0)
    {
        _test_fail("POST with body", "no response");
    }
    else
    {
        _test_pass("POST with body");
    }

    /*=========================================================================
     * Cleanup and heap check
     *========================================================================*/
    bHttpDeInit(httpfd);
    /* Give client task a few ticks to self-destruct. */
    bTaskDelayMs(pt, 100);

    mem_after = bGetFreeSize();
    b_log("[TEST-HTTP] heap after:  %d\r\n", mem_after);
    if (mem_after < mem_before - 256)
    {
        _test_fail("memory leak", "heap dropped significantly");
    }
    else
    {
        _test_pass("no significant memory leak");
    }

    s_stage = TST_DONE;
    test_http_client_done = 1;
    if (s_fail == 0)
    {
        b_log("[TEST-HTTP] ALL TESTS PASSED\r\n");
    }
    else
    {
        b_log_e("[TEST-HTTP] %d TEST(S) FAILED\r\n", s_fail);
    }

    /* Stop mock server so the binary can exit cleanly. */
    s_mock_running = 0;
    if (s_mock_sock >= 0)
    {
        shutdown(s_mock_sock, SHUT_RDWR);
    }

    bTaskRemove(s_test_task_id);
    PT_END(pt);
}

/* Public init called from utc_main.c --------------------------------------- */
void test_http_client_start(void)
{
    static pthread_t   server_thread;
    static int         server_port = TEST_HTTP_PORT;
    static bTaskAttr_t test_task_attr;

    pthread_create(&server_thread, NULL, _test_http_server_thread, &server_port);
    s_test_task_id = bTaskCreate("http_test", bHttpClientTestTask, NULL, &test_task_attr);
}
