/**
 *!
 * \file        b_srv_transfile.c
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
#include "services/inc/b_srv_transfile.h"

#if (defined(_TRANSFILE_SERVICE_ENABLE) && (_TRANSFILE_SERVICE_ENABLE == 1))
#include "algorithm/inc/algo_crc.h"
#include "core/inc/b_core.h"
#include "core/inc/b_sem.h"
#include "core/inc/b_task.h"
#include "drivers/inc/b_driver.h"
#include "hal/inc/b_hal.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup TRANSFILE
 * \{
 */

/**
 * \defgroup TRANSFILE_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TRANSFILE_Private_Defines
 * \{
 */
#ifndef TRANSFILE_TIMEOUT_S
#define TRANSFILE_TIMEOUT_S (30)
#endif

#define TFL_KLV_FILENAME_ID (0xAA01)
#define TFL_KLV_FILEVALUE_ID (0xAA02)

/**
 * \}
 */

/**
 * \defgroup TRANSFILE_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TRANSFILE_Private_Variables
 * \{
 */

static bProtSrvId_t      bProtocolId   = NULL;
static bTFLSrvSendData_t bProtocolSend = NULL;
static uint32_t          bFileOffset   = 0;

static bProtoFileInfo_t bCurrentFileInfo = {
    .size   = 0,
    .fcrc32 = 0,
};

static bProtoFileLocation_t bCurrentLocation = {
    .dev = 0,
};

B_TASK_CREATE_ATTR(bTFLTaskAttr);
B_SEM_CREATE_ATTR(bTFLSemAttr);
static bTaskId_t bTFLTaskId = NULL;
static bSemId_t  bTFLSemId  = NULL;

static bProtSrvSubscribe_t bSubInfo = {
    .number = 0,
};

const static bProtoCmd_t bCmdTable[] = {B_PROTO_TRANS_FILE_INFO, B_PROTO_SET_FILE_LOCATION,
                                        B_PROTO_FILE_DATA};

/**
 * \}
 */

/**
 * \defgroup TRANSFILE_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TRANSFILE_Private_Functions
 * \{
 */

static int _bTFLSendResult(uint8_t result)
{
    uint8_t  buf[128];
    uint16_t olen = 0;
    buf[0]        = result;
    olen          = bProtSrvPackage(bProtocolId, B_PROTO_TRANS_FILE_RESULT, buf, sizeof(buf));
    if (olen > 0 && bProtocolSend != NULL)
    {
        bProtocolSend(buf, olen);
    }
    return 0;
}

static void _bTFLEnd()
{
    bProtSrvUnsubscribe(bProtocolId, &bSubInfo);
    bTaskRemove(bTFLTaskId);
    bTFLTaskId = NULL;
    memset(&bCurrentFileInfo, 0, sizeof(bCurrentFileInfo));
    memset(&bCurrentLocation, 0, sizeof(bCurrentLocation));
    bProtocolId = NULL;
}

static int _bTFLCheck()
{
    uint32_t tmp_len = 0;
    uint8_t  tmp[64];
    uint32_t r_len = 0;
    CRC_REG_SBS_HANDLE(tmp_crc, ALGO_CRC32);
    int fd = bOpen(bCurrentLocation.dev, BCORE_FLAG_RW);
    if (fd == -1)
    {
        return -1;
    }
    bLseek(fd, bCurrentLocation.offset);
    while (tmp_len < bCurrentFileInfo.size)
    {
        r_len = ((bCurrentFileInfo.size - tmp_len) > 64) ? 64 : (bCurrentFileInfo.size - tmp_len);
        bRead(fd, tmp, r_len);
        crc_calculate_sbs(&tmp_crc, tmp, r_len);
        tmp_len += r_len;
    }
    bClose(fd);
    if (tmp_crc.crc != bCurrentFileInfo.fcrc32)
    {
        return -1;
    }
    return 0;
}

PT_THREAD(bTFLTaskFunc)(struct pt *pt, void *arg)
{
    uint8_t              buf[128];
    uint16_t             len     = 0;
    bProtoReqFileData_t *p_req   = (bProtoReqFileData_t *)buf;
    static uint32_t      timeout = 0;
    B_TASK_INIT_BEGIN();
    timeout = bHalGetSysTick();
    B_TASK_INIT_END();
    PT_BEGIN(pt);
    while (1)
    {
        bTaskDelayMs(pt, 200);

        if (bCurrentLocation.dev == 0 || bCurrentFileInfo.size == 0)
        {
            bTaskRestart(pt);
        }

        if (bFileOffset >= bCurrentFileInfo.size)
        {
            if (_bTFLCheck() == 0)
            {
                _bTFLSendResult(B_PROTO_TRANS_RESULT_OK);
            }
            else
            {
                _bTFLSendResult(B_PROTO_TRANS_RESULT_CHECKSUM_FAIL);
            }
            _bTFLEnd();
            bTaskRestart(pt);
        }
        p_req->offset = bFileOffset;
        p_req->size   = 512;
        len           = bProtSrvPackage(bProtocolId, B_PROTO_REQ_FILE_DATA, buf, sizeof(buf));
        bProtocolSend(buf, len);
        bSemAcquireBlock(pt, bTFLSemId, 3000);
        if (PT_WAIT_IS_TIMEOUT(pt))
        {
            if (TICK_DIFF_BIT32(timeout, bHalGetSysTick()) >=
                (MS2TICKS(TRANSFILE_TIMEOUT_S * 1000)))
            {
                _bTFLSendResult(B_PROTO_TRANS_RESULT_TIMEOUT);
                _bTFLEnd();
                bTaskRestart(pt);
            }
        }
        else
        {
            timeout = bHalGetSysTick();
        }
    }
    PT_END(pt);
}

static int _TFLProtCallback(bProtoCmd_t cmd, void *param)
{
    if (cmd == B_PROTO_TRANS_FILE_INFO)
    {
        bProtoFileInfo_t *pinfo = (bProtoFileInfo_t *)param;
        if (pinfo->size == 0)
        {
            return -1;
        }
        memcpy(&bCurrentFileInfo, param, sizeof(bCurrentFileInfo));
        bFileOffset = 0;
    }
    else if (cmd == B_PROTO_SET_FILE_LOCATION)
    {
        bProtoFileLocation_t *plocation = (bProtoFileLocation_t *)param;
        if (plocation->dev == 0)
        {
            return -1;
        }
        memcpy(&bCurrentLocation, plocation, sizeof(bCurrentLocation));
        bFileOffset = 0;
    }
    else if (cmd == B_PROTO_FILE_DATA)
    {
        if (bCurrentLocation.dev == 0 || bCurrentFileInfo.size == 0)
        {
            return -1;
        }
        bProtoFileData_t *pFileData = (bProtoFileData_t *)param;
        if (pFileData->size > 0 && pFileData->offset == bFileOffset)
        {
            int fd = -1;
            fd     = bOpen(bCurrentLocation.dev, BCORE_FLAG_RW);
            if (fd < 0)
            {
                return -1;
            }
            if (bFileOffset == 0)
            {
                uint32_t sector_size = 0;
                bCtl(fd, bCMD_GET_SECTOR_SIZE, &sector_size);
                if (sector_size == 0)
                {
                    bClose(fd);
                    return -1;
                }
                bFlashErase_t eparam;
                eparam.addr = bCurrentLocation.offset;
                eparam.num  = (bCurrentFileInfo.size + sector_size - 1) / sector_size;
                bCtl(fd, bCMD_ERASE_SECTOR, &eparam);
            }
            bLseek(fd, bCurrentLocation.offset + bFileOffset);
            uint32_t w_len = ((bCurrentFileInfo.size - bFileOffset) > pFileData->size)
                                 ? pFileData->size
                                 : (bCurrentFileInfo.size - bFileOffset);
            bWrite(fd, pFileData->dat, w_len);
            bFileOffset += w_len;
            bClose(fd);
            bSemRelease(bTFLSemId);
        }
    }
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup TRANSFILE_Exported_Functions
 * \{
 */

int bTFLSrvInit(bProtSrvId_t protocol_id, bTFLSrvSendData_t send)
{
    int ret = 0;
    if (protocol_id == NULL || send == NULL)
    {
        return -1;
    }
    bProtocolId         = protocol_id;
    bProtocolSend       = send;
    bFileOffset         = 0;
    bSubInfo.number     = sizeof(bCmdTable) / sizeof(bProtoCmd_t);
    bSubInfo.pcmd_table = &bCmdTable[0];
    bSubInfo.callback   = _TFLProtCallback;
    bProtSrvSubscribe(protocol_id, &bSubInfo);
    memset(&bCurrentFileInfo, 0, sizeof(bCurrentFileInfo));
    memset(&bCurrentLocation, 0, sizeof(bCurrentLocation));
    if (bTFLTaskId == NULL)
    {
        bTFLTaskId = bTaskCreate("transfile", bTFLTaskFunc, NULL, &bTFLTaskAttr);
    }
    if (bTFLSemId == NULL)
    {
        bTFLSemId = bSemCreate(1, 0, &bTFLSemAttr);
    }
    bSemAcquireNonblock(bTFLSemId);
    return 0;
}

int bTFLSrvDeinit()
{
    int ret = -1;
    if (bProtocolId == NULL)
    {
        return -1;
    }
    _bTFLEnd();
    return 0;
}

int bTFLSrvGetFileInfo(uint32_t dev_no, uint32_t base_addr, const char *filename,
                       bTFLSrvFileInfo_t *pinfo)
{
    uint16_t key = 0;
    uint32_t len = 0;
    uint8_t  tmp_buf[16];
    uint8_t  file_name_ok = 0;
    uint8_t  i            = 0;
    if (dev_no == 0 || filename == NULL || pinfo == NULL)
    {
        return -1;
    }
    int fd           = -1;
    int filename_len = strlen(filename);
    fd               = bOpen(dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -1;
    }
    while (1)
    {
        bLseek(fd, base_addr);
        bRead(fd, (uint8_t *)&key, sizeof(key));
        base_addr += sizeof(key);
        key = B_SWAP_16(key);
        if (key != TFL_KLV_FILENAME_ID && key != TFL_KLV_FILEVALUE_ID)
        {
            bClose(fd);
            return -1;
        }
        bRead(fd, (uint8_t *)&len, sizeof(len));
        base_addr += sizeof(len);
        len = B_SWAP_32(len);
        if (key == TFL_KLV_FILENAME_ID && len == filename_len)
        {
            int tmp_i = 0;
            while (tmp_i < len)
            {
                bRead(fd, tmp_buf, 16);
                for (i = 0; i < 16; i++)
                {
                    if (filename[tmp_i + i] == '\0')
                    {
                        file_name_ok = 1;
                        break;
                    }
                    if (filename[tmp_i + i] != tmp_buf[i])
                    {
                        break;
                    }
                }
                if (i < 16)
                {
                    break;
                }
                tmp_i += 16;
            }
        }
        else if (key == TFL_KLV_FILEVALUE_ID && file_name_ok == 1)
        {
            pinfo->addr = base_addr;
            pinfo->len  = len;
            break;
        }
        base_addr += len;
    }
    bClose(fd);
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
