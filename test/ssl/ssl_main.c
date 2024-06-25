/**
 * \file kv_main.c
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */
#include <time.h>

#include "../port.h"
#include "b_os.h"

int  httpfd = -1;
void bMallocFailedHook()
{
    b_log_e("=========================\r\n");
}

void HttpCb(bHttpEvent_t event, void *param, void *arg)
{
    if (event == B_HTTP_EVENT_RECV_DATA)
    {
        bHttpRecvData_t *dat = (bHttpRecvData_t *)param;
        if (dat->pdat != NULL && dat->len > 0)
        {
            b_log("%s\r\n", dat->pdat);
            char *pstr = strstr(dat->pdat, "\r\n\r\n");
            b_log("result: %s\r\n", pstr);
        }
        if (dat)
        {
            if (dat->release)
            {
                dat->release(dat->pdat);
            }
        }
    }
}

#define BOUNDARY_STR "----WebKitFormBoundary7MA4YWxkTrZu0gW"
static char *__pack_post_body(const char *api_key, const char *api_secret, const char *image_url)
{
#define BODY_PARAM_NUMBER (3)
#define BODY_ITEM_FORMAT "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n"
#define BODY_END_FORMAT "--%s--\r\n"
    int body_len = (strlen(BOUNDARY_STR) + strlen("Content-Disposition: form-data; name=") + 16) *
                       BODY_PARAM_NUMBER +
                   4 + strlen(BOUNDARY_STR) + 4 + strlen("api_key") + strlen("api_secret") +
                   strlen("image_url") + strlen(api_key) + strlen(api_secret) + strlen(image_url);
    b_log("malloc: %d\r\n", body_len);
    char *pbody = bMalloc(body_len);
    b_assert_log(pbody != NULL);
    char *ptmp = pbody;
    int   ret  = 0;
    ret        = sprintf(ptmp, BODY_ITEM_FORMAT, BOUNDARY_STR, "api_key", api_key);
    ptmp += ret;
    ret = sprintf(ptmp, BODY_ITEM_FORMAT, BOUNDARY_STR, "api_secret", api_secret);
    ptmp += ret;
    ret = sprintf(ptmp, BODY_ITEM_FORMAT, BOUNDARY_STR, "image_url", image_url);
    ptmp += ret;
    ret = sprintf(ptmp, BODY_END_FORMAT, BOUNDARY_STR);
    b_log("body[%d]:\r\n%s", strlen(pbody), pbody);
    return pbody;
}

int main()
{
    port_init();
    bInit();

    char  post_header[128];
    char *pbody = NULL;

    sprintf(post_header, "Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY_STR);
    pbody = __pack_post_body(
        "babyos", "babyos",
        "https://tse1-mm.cn.bing.net/th/id/OIP-C.Y2M7yqEjWKR0mrPD9xp9tAAAAA?rs=1&pid=ImgDetMain");

    httpfd = bHttpInit(HttpCb, NULL);
    bHttpRequest(httpfd, B_HTTP_POST, "https://aiyan-tech.com/car_number/", post_header, pbody);
    bFree(pbody);
    while (1)
    {
        bExec();
    }
    return 0;
}
