/**
 *!
 * \file        b_srv_ota.c
 * \version     v0.0.1
 * \date        2023/08/27
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 Bean
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "services/inc/b_srv_ota.h"

#if (defined(_OTA_SERVICE_ENABLE) && (_OTA_SERVICE_ENABLE == 1))

#include "algorithm/inc/algo_crc.h"
#include "core/inc/b_sem.h"
#include "core/inc/b_task.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup OTA
 * \{
 */

/**
 * \defgroup OTA_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup OTA_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup OTA_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup OTA_Private_Variables
 * \{
 */

static bProtSrvId_t      bProtocolId    = NULL;
static bOtaSrvSendData_t bProtocolSend  = NULL;
static uint32_t          bOtaFileOffset = 0;

B_TASK_CREATE_ATTR(bOtaTaskAttr);
B_SEM_CREATE_ATTR(bOtaSemAttr);
static bTaskId_t bOtaTaskId = NULL;
static bSemId_t  bOtaSemId  = NULL;

static bProtSrvSubscribe_t bSubInfo = {
    .number = 0,
};

const static bProtoCmd_t bCmdTable[] = {B_PROTO_OTA_FILE_INFO, B_PROTO_FILE_DATA};

/**
 * \}
 */

/**
 * \defgroup OTA_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup OTA_Private_Functions
 * \{
 */

PT_THREAD(bOtaTaskFunc)(struct pt *pt, void *arg)
{
    uint8_t              buf[128];
    uint16_t             len   = 0;
    bProtoReqFileData_t *p_req = (bProtoReqFileData_t *)buf;
    PT_BEGIN(pt);
    while (1)
    {
        bTaskDelayMs(pt, 200);
        p_req->offset = bOtaFileOffset;
        p_req->size   = 512;
        len           = bProtSrvPackage(bProtocolId, B_PROTO_REQ_FILE_DATA, buf, sizeof(buf));
        bProtocolSend(buf, len);
        bSemAcquireBlock(pt, bOtaSemId, 3000);
        if (bOtaTaskId != NULL && (bIapGetStatus() != B_IAP_STA_START))
        {
            bTaskRemove(bOtaTaskId);
        }
    }
    PT_END(pt);
}

static void _OtaProtStart()
{
    if (bOtaTaskId != NULL)
    {
        bTaskRemove(bOtaTaskId);
    }
    bOtaTaskId = bTaskCreate("ota", bOtaTaskFunc, NULL, &bOtaTaskAttr);
    if (bOtaSemId == NULL)
    {
        bOtaSemId = bSemCreate(1, 0, &bOtaSemAttr);
    }
    bSemAcquireNonblock(bOtaSemId);
    bOtaFileOffset = 0;
}

static int _OtaProtCallback(bProtoCmd_t cmd, void *param)
{
    if (cmd == B_PROTO_OTA_FILE_INFO)
    {
        bProtoFileInfo_t *file_info = (bProtoFileInfo_t *)param;
        bIapFwInfo_t      fwinfo;
        fwinfo.crc      = file_info->fcrc32;
        fwinfo.crc_type = ALGO_CRC32;
        fwinfo.len      = file_info->size;
        memcpy(fwinfo.name, file_info->name, sizeof(fwinfo.name));
        bIapEventHandler(B_IAP_EVENT_START, &fwinfo);
        _OtaProtStart();
    }
    else if (cmd == B_PROTO_FILE_DATA)
    {
        bProtoFileData_t *pFileData = (bProtoFileData_t *)param;
        if (pFileData->size > 0 && pFileData->offset == bOtaFileOffset)
        {
            bIapFwData_t fwdata;
            fwdata.pbuf    = pFileData->dat;
            fwdata.len     = pFileData->size;
            fwdata.release = NULL;
            int ret        = bIapEventHandler(B_IAP_EVENT_DATA, &fwdata);
            if (ret == 0)
            {
                bOtaFileOffset += pFileData->size;
            }
            else if (ret == 2)
            {
                bOtaFileOffset = 0;
            }
        }
        if ((bIapGetStatus() == B_IAP_STA_READY) && (bIapIsInBoot() == 0))
        {
            bIapJump2Boot();
        }
        else if (bIapGetStatus() == B_IAP_STA_START)
        {
            bSemRelease(bOtaSemId);
        }
    }
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup OTA_Exported_Functions
 * \{
 */

int bOtaSrvInit(bProtSrvId_t protocol_id, bOtaSrvSendData_t send, uint32_t cache_dev_no,
                uint32_t backup_dev_no, uint32_t backup_time_s)
{
    int ret = 0;
    if (protocol_id == NULL || send == NULL)
    {
        return -1;
    }
    bProtocolId         = protocol_id;
    bProtocolSend       = send;
    ret                 = bIapInit(cache_dev_no, backup_dev_no, backup_time_s);
    bOtaFileOffset      = 0;
    bSubInfo.number     = sizeof(bCmdTable) / sizeof(bProtoCmd_t);
    bSubInfo.pcmd_table = &bCmdTable[0];
    bSubInfo.callback   = _OtaProtCallback;
    bProtSrvSubscribe(protocol_id, &bSubInfo);
    if ((bIapIsInBoot()) && (bIapGetStatus() == B_IAP_STA_START))
    {
        _OtaProtStart();
    }
    if ((bIapIsInBoot() == 0) && (ret == 1))
    {
        uint8_t  buf[128];
        uint16_t olen   = 0;
        uint8_t  result = 0;
        buf[0]          = result;
        olen            = bProtSrvPackage(bProtocolId, B_PROTO_TRANS_FILE_RESULT, buf, sizeof(buf));
        if (olen > 0)
        {
            send(buf, olen);
        }
    }
    return 0;
}

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */
#endif

/************************ Copyright (c) 2023 Bean *****END OF FILE****/
