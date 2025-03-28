#include "modules/inc/b_mod_netif/b_mod_trans.h"

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))

#if (defined(VENDOR_UBUNTU) && (VENDOR_UBUNTU == 1))

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

typedef struct
{
    int          sockfd;
    void        *cb_arg;
    pbTransCb_t  callback;
    uint8_t      readable;
    uint8_t      writeable;
    bFIFO_Info_t rx_fifo;
    uint16_t     local_port;
    uint16_t     remote_port;
    uint32_t     remote_ip;
    bTransType_t type;
    pthread_t    thread;
} bTrans_t;

void *thread_function(void *arg)
{
    uint8_t   buf[CONNECT_RECVBUF_MAX];
    bTrans_t *ptrans = (bTrans_t *)arg;
    int       retval = -1;

    int flags = fcntl(ptrans->sockfd, F_GETFL, 0);
    if (fcntl(ptrans->sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        b_log("fcntl fail\r\n");
        return -1;
    }

    int                epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = ptrans->sockfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ptrans->sockfd, &ev) == -1)
    {
        b_log("epoll_ctl fail\r\n");
        return NULL;
    }
    while (1)
    {
        if (epoll_wait(epoll_fd, &ev, 1, -1) == -1)
        {
            b_log("epoll_wait fail\r\n");
            return NULL;
        }
        if (ev.data.fd == ptrans->sockfd && ev.events & EPOLLIN)
        {
            retval = recv(ptrans->sockfd, buf, sizeof(buf), 0);
            if (retval == -1 || retval == 0)
            {
                ;
            }
            else if (retval > 0)
            {
                b_log("recv:%d ...\r\n", retval);
                b_log_hex(buf, retval);
                bFIFO_Write(&ptrans->rx_fifo, buf, retval);
                ptrans->readable = 1;
                ptrans->callback(B_TRANS_NEW_DATA, ptrans, ptrans->cb_arg);
            }
        }
    }
    pthread_exit(NULL);
}

int bSocket(bTransType_t type, pbTransCb_t cb, void *user_data)
{
    int result;
    if (cb == NULL || (type != B_TRANS_CONN_TCP && type != B_TRANS_CONN_UDP))
    {
        return -1;
    }
    int sockfd = -1;
    sockfd     = socket(AF_INET, (type == B_TRANS_CONN_TCP) ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        b_log_e("Error: socket creation failed\r\n");
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)bMalloc(sizeof(bTrans_t));
    if (ptrans == NULL)
    {
        close(sockfd);
        return -2;
    }
    memset(ptrans, 0, sizeof(bTrans_t));
    uint8_t *pbuf = (uint8_t *)bMalloc(CONNECT_RECVBUF_MAX);
    if (pbuf == NULL)
    {
        bFree(ptrans);
        ptrans = NULL;
        return -2;
    }
    bFIFO_Init(&ptrans->rx_fifo, pbuf, CONNECT_RECVBUF_MAX);
    ptrans->sockfd    = sockfd;
    ptrans->type      = type;
    ptrans->callback  = cb;
    ptrans->cb_arg    = user_data;
    ptrans->readable  = 0;
    ptrans->writeable = 0;
    return (int)ptrans;
}

int bConnect(int sockfd, char *remote, uint16_t port)
{
    if (SOCKFD_IS_INVALID(sockfd) || remote == NULL)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;

    struct hostent  *he;
    struct in_addr **addr_list;

    if ((he = gethostbyname(remote)) == NULL)
    {
        b_log("gethostbyname fail\r\n");
        return -1;
    }
    addr_list = (struct in_addr **)he->h_addr_list;
    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(port);
    serverAddr.sin_addr.s_addr = addr_list[0]->s_addr;

    // 连接到服务器
    if (connect(ptrans->sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        b_log("Error: connection failed\r\n");
        ptrans->callback(B_TRANS_ERROR, ptrans, ptrans->cb_arg);
        return -1;
    }
    ptrans->writeable = 1;
    ptrans->callback(B_TRANS_CONNECTED, ptrans, ptrans->cb_arg);
    pthread_create(&ptrans->thread, NULL, thread_function, ptrans);
    b_log("Connected to the server.\r\n");
    return 0;
}

int bBind(int sockfd, uint16_t port)
{
    return -1;
}

int bListen(int sockfd, int backlog)
{
    return -1;
}

int bRecv(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen)
{
    int      read_len = 0;
    uint16_t fifo_len = 0;
    if (SOCKFD_IS_INVALID(sockfd) || pbuf == NULL || buf_len == 0)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    read_len         = bFIFO_Read(&ptrans->rx_fifo, pbuf, buf_len);
    if (read_len < 0)
    {
        return -2;
    }
    if (rlen)
    {
        *rlen = (uint16_t)(read_len & 0xffff);
    }
    bFIFO_Length(&ptrans->rx_fifo, &fifo_len);
    if (fifo_len == 0)
    {
        ptrans->readable = 0;
    }
    return 0;
}

int bSend(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen)
{
    uint16_t writeable_len = 0;
    int      retval        = -1;
    if (SOCKFD_IS_INVALID(sockfd) || pbuf == NULL || buf_len == 0)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    retval           = send(ptrans->sockfd, pbuf, buf_len, 0);
    if (retval < 0)
    {
        return -1;
    }
    b_log("send %d \r\n", retval);
    if (retval >= 0 && wlen != NULL)
    {
        *wlen = retval;
    }
    return 0;
}

uint8_t bSockIsReadable(int sockfd)
{
    if (SOCKFD_IS_INVALID(sockfd))
    {
        return 0;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    return ptrans->readable;
}

uint8_t bSockIsWriteable(int sockfd)
{
    if (SOCKFD_IS_INVALID(sockfd))
    {
        return 0;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    return ptrans->writeable;
}

int bShutdown(int sockfd)
{
    int retval = -1;
    if (SOCKFD_IS_INVALID(sockfd))
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    retval           = pthread_cancel(ptrans->thread);
    b_log("retval:%d\r\n", retval);
    close(ptrans->sockfd);
    bFree(ptrans->rx_fifo.pbuf);
    bFIFO_Deinit(&ptrans->rx_fifo);
    bFree(ptrans);
    ptrans = NULL;
    return 0;
}

int bDnsParse(char *remote, pbTransDnsCb_t cb, void *user_data)
{
    if (remote == NULL || cb == NULL)
    {
        return -1;
    }
    struct hostent  *he;
    struct in_addr **addr_list;

    if ((he = gethostbyname(remote)) == NULL)
    {
        b_log("gethostbyname fail\r\n");
        return -1;
    }
    addr_list = (struct in_addr **)he->h_addr_list;
    cb(remote, addr_list[0]->s_addr, user_data);
    return 0;
}
int bPing(char *remote, uint32_t timeout_ms, pbTransPingCb_t cb, void *user_data)
{
    return -1;
}

#endif

#endif
