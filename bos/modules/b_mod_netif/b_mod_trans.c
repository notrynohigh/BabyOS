#include "modules/inc/b_mod_netif/b_mod_trans.h"

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
#if defined(__WEAKDEF)
__WEAKDEF int bSocket(bTransType_t type, pbTransCb_t cb, void *user_data)
{
    return -1;
}
__WEAKDEF int bConnect(int sockfd, char *remote, uint16_t port)
{
    return -1;
}
__WEAKDEF int bBind(int sockfd, uint16_t port)
{
    return -1;
}
__WEAKDEF int bListen(int sockfd, int backlog)
{
    return -1;
}
__WEAKDEF int bRecv(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen)
{
    return -1;
}
__WEAKDEF int bSend(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen)
{
    return -1;
}
__WEAKDEF int bShutdown(int sockfd)
{
    return -1;
}
__WEAKDEF uint8_t bSockIsReadable(int sockfd)
{
    return 0;
}
__WEAKDEF uint8_t bSockIsWriteable(int sockfd)
{
    return 0;
}
__WEAKDEF int bDnsParse(char *remote, pbTransDnsCb_t cb, void *user_data)
{
    return -1;
}
__WEAKDEF int bPing(char *remote, uint32_t timeout_ms, pbTransPingCb_t cb, void *user_data)
{
    return -1;
}
#endif

#endif
