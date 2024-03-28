#include "b_config.h"
#include "hal/inc/b_hal_eth.h"

int bMcuEthInit(bHalBufList_t *pbuf_list)
{
    return -1;
}

int bMcuEthGetMacAddr(uint8_t *paddr, uint8_t len)
{
    return -1;
}

uint8_t bMcuEthIsLinked()
{
    return 0;
}

int bMcuEthLinkUpdate(uint8_t link_state)
{
    return 0;
}

int bMcuEthReceive(void **pbuf, uint32_t *plen)
{
    return -1;
}

int bMcuEthTransmit(void *pbuf, uint32_t len)
{
    return -1;
}

