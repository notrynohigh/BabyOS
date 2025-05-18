/**
 *!
 * \file        b_mod_protocol.c
 * \version     v0.1.0
 * \date        2020/03/15
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
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
#include "modules/inc/b_mod_protocol.h"
#if (defined(_PROTO_ENABLE) && (_PROTO_ENABLE == 1))
#include <string.h>

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup PROTOCOL
 * \{
 */

/**
 * \defgroup PROTOCOL_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Defines
 * \{
 */
#if (defined(_PROTO_ENCRYPT_ENABLE) && (_PROTO_ENCRYPT_ENABLE == 1))
#define _PROTO_TEA_DELTA 0x9e3779b9
const static uint32_t Keys[4] = {SECRET_KEY1, SECRET_KEY2, SECRET_KEY3, SECRET_KEY4};
#endif
/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup PROTOCOL_Private_Functions
 * \{
 */
static uint8_t _bProtocolCalCheck(uint8_t *pbuf, bProtoLen_t len)
{
    uint8_t     tmp;
    bProtoLen_t i;
    tmp = pbuf[0];
    for (i = 1; i < len; i++)
    {
        tmp += pbuf[i];
    }
    return tmp;
}

#if (defined(_PROTO_ENCRYPT_ENABLE) && (_PROTO_ENCRYPT_ENABLE == 1))
static void _bProtocolEncryptGroup(uint32_t *text, uint32_t *key)
{
    uint32_t sum = 0, v0 = text[0], v1 = text[1];
    uint32_t k0 = key[0], k1 = key[1], k2 = key[2], k3 = key[3];
    int      i = 0;

    for (i = 0; i < 16; i++)
    {
        sum += _PROTO_TEA_DELTA;
        v0 += (v1 << 4) + k0 ^ v1 + sum ^ (v1 >> 5) + k1;
        v1 += (v0 << 4) + k2 ^ v0 + sum ^ (v0 >> 5) + k3;
    }
    text[0] = v0;
    text[1] = v1;
}

static void _bProtocolDecryptGroup(uint32_t *text, uint32_t *key)
{
    uint32_t sum = _PROTO_TEA_DELTA * 16, v0 = text[0], v1 = text[1];
    uint32_t k0 = key[0], k1 = key[1], k2 = key[2], k3 = key[3];
    int      i = 0;

    for (i = 0; i < 16; i++)
    {
        v1 -= (v0 << 4) + k2 ^ v0 + sum ^ (v0 >> 5) + k3;
        v0 -= (v1 << 4) + k0 ^ v1 + sum ^ (v1 >> 5) + k1;
        sum -= _PROTO_TEA_DELTA;
    }
    text[0] = v0;
    text[1] = v1;
}

static void _bProtocolEncrypt(uint8_t *text, uint32_t size)
{
    uint32_t number = size >> 3;
    int      i      = 0;

    if (size < 8)
    {
        return;
    }
    for (i = 0; i < number; i++)
    {
        _bProtocolEncryptGroup(&(((uint32_t *)text)[i * 2]), (uint32_t *)Keys);
    }
}

static void _bProtocolDecrypt(uint8_t *text, uint32_t size)
{
    uint32_t number = size >> 3;
    int      i      = 0;

    if (size < 8)
    {
        return;
    }

    for (i = 0; i < number; i++)
    {
        _bProtocolDecryptGroup(&(((uint32_t *)text)[i * 2]), (uint32_t *)Keys);
    }
}
#endif

static int _bProtocolPack(bProtocolAttr_t *pattr, uint8_t cmd, uint8_t *param,
                          bProtoLen_t param_size, uint8_t *pbuf, uint16_t buf_len)
{
    int              length = 0;
    bProtoID_t       id     = 0;
    bProtocolHead_t *phead;

    if (pbuf == NULL)
    {
        return 0;
    }

    if ((param == NULL && param_size > 0) || ((sizeof(bProtocolHead_t) + param_size + 1) > buf_len))
    {
        return -1;
    }
    B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_DEVICE_ID, (uint8_t *)&id, sizeof(bProtoID_t));
    phead            = (bProtocolHead_t *)pbuf;
    phead->head      = PROTOCOL_HEAD;
    phead->device_id = id;
    phead->cmd       = cmd;
    phead->len       = 1 + param_size;
    memcpy(&pbuf[sizeof(bProtocolHead_t)], param, param_size);
    length           = sizeof(bProtocolHead_t) + phead->len;
    pbuf[length - 1] = _bProtocolCalCheck(pbuf, length - 1);
#if (defined(_PROTO_ENCRYPT_ENABLE) && (_PROTO_ENCRYPT_ENABLE == 1))
    _bProtocolEncrypt(pbuf, length);
#endif
    return length;
}

static int _bProtocolParse(void *attr, uint8_t *in, uint16_t i_len, uint8_t *out, uint16_t o_len)
{
    bProtocolAttr_t *pattr = (bProtocolAttr_t *)attr;
    bProtocolHead_t *phead = (bProtocolHead_t *)in;
    bProtoID_t       id    = 0;
    int              length;
    uint8_t          crc;

    uint8_t *reply_p   = NULL;
    uint16_t reply_len = 0;

    if (in == NULL || i_len < (sizeof(bProtocolHead_t) + 1))
    {
        return -1;
    }
#if (defined(_PROTO_ENCRYPT_ENABLE) && (_PROTO_ENCRYPT_ENABLE == 1))
    _bProtocolDecrypt(in, i_len);
#endif
    B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_DEVICE_ID, (uint8_t *)&id, sizeof(bProtoID_t));
    if (phead->head != PROTOCOL_HEAD ||
        (phead->device_id != id && (phead->device_id != INVALID_ID) && (id != INVALID_ID)))
    {
        return -1;
    }
    length = phead->len + sizeof(bProtocolHead_t);
    if (length > i_len)
    {
        return -1;
    }
    crc = _bProtocolCalCheck(in, length - 1);
    if (crc != in[length - 1])
    {
        b_log_e("crc error!%d %d", crc, in[length - 1]);
        return -1;
    }
#if (defined(PROTO_ROLE_HOST) && (PROTO_ROLE_HOST == 1))
    uint8_t              tmp_value = 0;
    bProtoNetInfoParam_t tmp_netinfo;
    if (phead->cmd == PROTO_CMD_SETCFGNET_MODE)
    {
        bProtoCfgNetMode_t info;
        memset(&info, 0, sizeof(info));
        bProtoCfgNetModeParam_t *cfg = (bProtoCfgNetModeParam_t *)(&in[sizeof(bProtocolHead_t)]);

        info.type = cfg->type;
        memcpy(info.ssid, cfg->ssid, strlen((const char *)cfg->ssid));
        memcpy(info.passwd, cfg->passwd, strlen((const char *)cfg->passwd));
        B_SAFE_INVOKE(pattr->callback, B_PROTO_SETCFGNET_MODE, &info, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_GET_NETINFO)
    {
        memset(&tmp_netinfo, 0, sizeof(bProtoNetInfoParam_t));
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_GET_SSID, &tmp_netinfo.ssid[0],
                      sizeof(tmp_netinfo.ssid));
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_GET_IP, (uint8_t *)&tmp_netinfo.ip,
                      sizeof(tmp_netinfo.ip));
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_GET_GW, (uint8_t *)&tmp_netinfo.gw,
                      sizeof(tmp_netinfo.gw));
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_GET_MASK, (uint8_t *)&tmp_netinfo.mask,
                      sizeof(tmp_netinfo.mask));
        reply_p   = (uint8_t *)&tmp_netinfo;
        reply_len = sizeof(bProtoNetInfoParam_t);
    }
    else if (phead->cmd == PROTO_CMD_SET_VOICE_SWITCH)
    {
        uint8_t *param = (uint8_t *)(&in[sizeof(bProtocolHead_t)]);
        B_SAFE_INVOKE(pattr->callback, B_PROTO_SET_VOICE_SWITCH, param, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_SET_VOICE_VOLUME)
    {
        uint8_t *param = (uint8_t *)(&in[sizeof(bProtocolHead_t)]);
        B_SAFE_INVOKE(pattr->callback, B_PROTO_SET_VOICE_VOLUME, param, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_GET_VOICE_VOLUME)
    {
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_GET_VOICE_VOLUME, &tmp_value,
                      sizeof(tmp_value));
        reply_p   = (uint8_t *)&tmp_value;
        reply_len = sizeof(uint8_t);
    }
    else if (phead->cmd == PROTO_CMD_GET_VOICE_STAT)
    {
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_GET_VOICE_STAT, &tmp_value, sizeof(tmp_value));
        reply_p   = (uint8_t *)&tmp_value;
        reply_len = sizeof(uint8_t);
    }
    else if (phead->cmd == PROTO_CMD_DEVICEINFO)
    {
        bProtoDevInfo_t info;
        memset(&info, 0, sizeof(info));
        bProtoDevInfoParam_t *info_param = (bProtoDevInfoParam_t *)(&in[sizeof(bProtocolHead_t)]);

        memcpy(info.version, info_param->version, strlen((const char *)info_param->version));
        memcpy(info.name, info_param->name, strlen((const char *)info_param->name));
        B_SAFE_INVOKE(pattr->callback, B_PROTO_DEVICEINFO, &info, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_FDATA)
    {
        bProtoReqFileData_t reqfile;
        memset(&reqfile, 0, sizeof(reqfile));

        bProtoReqFDataParam_t *reqfile_param =
            (bProtoReqFDataParam_t *)(&in[sizeof(bProtocolHead_t)]);
        reqfile.offset = reqfile_param->seq * 512;
        reqfile.size   = 512;
        B_SAFE_INVOKE(pattr->callback, B_PROTO_REQ_FILE_DATA, &reqfile, pattr->arg);
    }
#else
    bProtoUIDParam_t     uid;
    bProtoDevInfoParam_t devinfo;
    if (phead->cmd == PROTO_CMD_TEST)
    {
        ;
    }
    else if (phead->cmd == PROTO_CMD_UTC)
    {
        bProtoUtc_t utc;
        uint32_t   *dat = (uint32_t *)(&in[sizeof(bProtocolHead_t)]);
        utc.utc         = *dat;
        utc.timezone    = 8.0;
        B_SAFE_INVOKE(pattr->callback, B_PROTO_UTC, &utc, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_FW_INFO)
    {
        bProtoFileInfo_t info;
        bProtoFWParam_t *fw = (bProtoFWParam_t *)(&in[sizeof(bProtocolHead_t)]);
        info.size           = fw->size;
        info.fcrc32         = fw->f_crc32;
        memcpy(&info.name[0], fw->filename, sizeof(fw->filename));
        B_SAFE_INVOKE(pattr->callback, B_PROTO_OTA_FILE_INFO, &info, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_TRANS_FILE)
    {
        bProtoFileInfo_t        info;
        bProtoTransFileParam_t *param = (bProtoTransFileParam_t *)(&in[sizeof(bProtocolHead_t)]);

        info.size   = param->size;
        info.fcrc32 = param->f_crc32;
        B_SAFE_INVOKE(pattr->callback, B_PROTO_TRANS_FILE_INFO, &info, pattr->arg);

        bProtoFileLocation_t location;
        location.dev    = param->dev_no;
        location.offset = param->offset;
        B_SAFE_INVOKE(pattr->callback, B_PROTO_SET_FILE_LOCATION, &location, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_FDATA)
    {
        bProtoFileData_t    dat;
        bProtoFDataParam_t *param = (bProtoFDataParam_t *)(&in[sizeof(bProtocolHead_t)]);
        dat.offset                = param->seq * 512;
        dat.size                  = 512;
        dat.dat                   = param->data;
        B_SAFE_INVOKE(pattr->callback, B_PROTO_FILE_DATA, &dat, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_WRITE_SN)
    {
        bProtoSN_t       dat;
        bProtoSNParam_t *param = (bProtoSNParam_t *)(&in[sizeof(bProtocolHead_t)]);
        dat.sn_len             = param->len;
        memcpy(&dat.sn[0], &param->sn[0], param->len);
        B_SAFE_INVOKE(pattr->callback, B_PROTO_WRITE_SN, &dat, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_GET_NETINFO)
    {
        bProtoNetInfo_t       dat;
        bProtoNetInfoParam_t *param = (bProtoNetInfoParam_t *)(&in[sizeof(bProtocolHead_t)]);
        memset(&dat, 0, sizeof(dat));
        dat.ip   = param->ip;
        dat.gw   = param->gw;
        dat.mask = param->mask;
        memcpy(dat.ssid, param->ssid, strlen((const char *)param->ssid));
        B_SAFE_INVOKE(pattr->callback, B_PROTO_GET_NETINFO, &dat, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_GET_VOICE_VOLUME)
    {
        uint8_t *param = (uint8_t *)(&in[sizeof(bProtocolHead_t)]);
        B_SAFE_INVOKE(pattr->callback, B_PROTO_GET_VOICE_VOLUME, param, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_GET_VOICE_STAT)
    {
        uint8_t *param = (uint8_t *)(&in[sizeof(bProtocolHead_t)]);
        B_SAFE_INVOKE(pattr->callback, B_PROTO_GET_VOICE_STATE, param, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_TTS_CONTENT)
    {
        uint8_t *param = (uint8_t *)(&in[sizeof(bProtocolHead_t)]);
        B_SAFE_INVOKE(pattr->callback, B_PROTO_TTS_CONTENT, param, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_TSL_INVOKE)
    {
        uint8_t *param = (uint8_t *)(&in[sizeof(bProtocolHead_t)]);
        B_SAFE_INVOKE(pattr->callback, B_PROTO_TSL_INVOKE, param, pattr->arg);
    }
    else if (phead->cmd == PROTO_CMD_GET_UID)
    {
        memset(&uid, 0, sizeof(bProtoUIDParam_t));
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_MCU_UID, (uint8_t *)&uid, sizeof(uid));
        if (uid.len == 0)
        {
            return 0;
        }
        reply_p   = (uint8_t *)&uid;
        reply_len = uid.len + 1;
    }
    else if (phead->cmd == PROTO_CMD_DEVICEINFO)
    {
        memset(&devinfo, 0, sizeof(bProtoDevInfoParam_t));
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_DEVICE_VERSION, &devinfo.version[0],
                      sizeof(devinfo.version));
        B_SAFE_INVOKE(pattr->get_info, B_PROTO_INFO_DEVICE_NAME, &devinfo.name[0],
                      sizeof(devinfo.name));
        if (strlen((const char *)devinfo.version) == 0 || strlen((const char *)devinfo.name) == 0)
        {
            return 0;
        }
        reply_p   = (uint8_t *)&devinfo;
        reply_len = sizeof(bProtoDevInfoParam_t);
    }
#endif
    length = 0;
    if (PROTOCOL_NEED_DEFAULT_ACK(phead->cmd))
    {
        length = _bProtocolPack(pattr, phead->cmd, NULL, 0, out, o_len);
    }
    else if (reply_p)
    {
        length = _bProtocolPack(pattr, phead->cmd, reply_p, reply_len, out, o_len);
    }

    return length;
}

static int _bShiftArray(uint8_t *raw, uint16_t len, uint16_t offset)
{
    if (len <= offset)
    {
        return -1;
    }
    for (int i = len - (offset + 1); i >= 0; i--)
    {
        raw[i + offset] = raw[i];
    }
    return 0;
}

static int _bProtocolPackage(void *attr, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len)
{
    int      ret = -1;
    uint8_t  trans_tmp_buf[128];
    uint8_t *tmp_buf   = NULL;
    uint16_t tmp_size  = 0;
    uint8_t  proto_cmd = 0;

    tmp_buf = &trans_tmp_buf[0];

    if (cmd == B_PROTO_REQ_FILE_DATA)
    {
        bProtoReqFileData_t req;
        req.offset                              = ((bProtoReqFileData_t *)buf)->offset;
        req.size                                = ((bProtoReqFileData_t *)buf)->size;
        ((bProtoReqFDataParam_t *)tmp_buf)->seq = req.offset / 512;
        tmp_size                                = sizeof(bProtoReqFDataParam_t);
        proto_cmd                               = PROTO_CMD_FDATA;
    }
    else if (cmd == B_PROTO_TRANS_FILE_RESULT)
    {
        uint8_t result                              = 0;
        result                                      = buf[0];
        ((bProtoOTAResultParam_t *)tmp_buf)->result = result;
        tmp_size                                    = sizeof(bProtoOTAResultParam_t);
        proto_cmd                                   = PROTO_CMD_OTA_RESULT;
    }
    else if (cmd == B_PROTO_SETCFGNET_MODE)
    {
        memset(trans_tmp_buf, 0, sizeof(trans_tmp_buf));
        bProtoCfgNetModeParam_t *cfgnet_param = (bProtoCfgNetModeParam_t *)tmp_buf;
        bProtoCfgNetMode_t      *pcfgnet      = (bProtoCfgNetMode_t *)buf;
        cfgnet_param->type                    = pcfgnet->type;
        memcpy(cfgnet_param->ssid, pcfgnet->ssid, strlen((const char *)pcfgnet->ssid));
        memcpy(cfgnet_param->passwd, pcfgnet->passwd, strlen((const char *)pcfgnet->passwd));
        tmp_size  = sizeof(bProtoCfgNetModeParam_t);
        proto_cmd = PROTO_CMD_SETCFGNET_MODE;
    }
    else if (cmd == B_PROTO_GET_NETINFO)
    {
        proto_cmd = PROTO_CMD_GET_NETINFO;
    }
    else if (cmd == B_PROTO_SET_VOICE_SWITCH)
    {
        proto_cmd  = PROTO_CMD_SET_VOICE_SWITCH;
        tmp_buf[0] = buf[0];
        tmp_size   = 1;
    }
    else if (cmd == B_PROTO_SET_VOICE_VOLUME)
    {
        proto_cmd  = PROTO_CMD_SET_VOICE_VOLUME;
        tmp_buf[0] = buf[0];
        tmp_size   = 1;
    }
    else if (cmd == B_PROTO_GET_VOICE_VOLUME)
    {
        proto_cmd = PROTO_CMD_GET_VOICE_VOLUME;
    }
    else if (cmd == B_PROTO_GET_VOICE_STATE)
    {
#if (defined(PROTO_ROLE_HOST) && (PROTO_ROLE_HOST == 1))
        proto_cmd  = PROTO_CMD_GET_VOICE_STAT;
        tmp_buf[0] = buf[0];
        tmp_size   = 1;
#else
        proto_cmd = PROTO_CMD_GET_VOICE_STAT;
#endif
    }
    else if (cmd == B_PROTO_TTS_CONTENT)
    {
        proto_cmd = PROTO_CMD_TTS_CONTENT;
        tmp_size  = strlen((const char *)buf);
        if (_bShiftArray(buf, buf_len, sizeof(bProtocolHead_t)) == 0)
        {
            tmp_buf = buf + sizeof(bProtocolHead_t);
        }
    }
    else if (cmd == B_PROTO_TSL_INVOKE)
    {
        proto_cmd = PROTO_CMD_TSL_INVOKE;
        tmp_size  = strlen((const char *)buf);
        if (_bShiftArray(buf, buf_len, sizeof(bProtocolHead_t)) == 0)
        {
            tmp_buf = buf + sizeof(bProtocolHead_t);
        }
    }
    else if (cmd == B_PROTO_DEVICEINFO)
    {
        proto_cmd = PROTO_CMD_DEVICEINFO;
    }
    else if (cmd == B_PROTO_OTA_FILE_INFO)
    {
        proto_cmd                  = PROTO_CMD_FW_INFO;
        bProtoFileInfo_t *pfwinfo  = (bProtoFileInfo_t *)buf;
        bProtoFWParam_t  *fw_param = (bProtoFWParam_t *)tmp_buf;
        fw_param->size             = pfwinfo->size;
        fw_param->f_crc32          = pfwinfo->fcrc32;
        memcpy(fw_param->filename, pfwinfo->name, strlen((const char *)pfwinfo->name));
        tmp_size = sizeof(bProtoFWParam_t);
    }
    else if (cmd == B_PROTO_FILE_DATA)
    {
        proto_cmd                   = PROTO_CMD_FDATA;
        bProtoFileData_t *file_data = (bProtoFileData_t *)buf;
        bProtoFileData_t  tmp_file_data;
        memcpy(&tmp_file_data, file_data, sizeof(bProtoFileData_t));

        bProtoFDataParam_t *fdata_param = (bProtoFDataParam_t *)buf;
        fdata_param->seq                = tmp_file_data.offset / sizeof(fdata_param->data);
        memcpy(fdata_param->data, tmp_file_data.dat, sizeof(fdata_param->data));
        if (_bShiftArray(buf, buf_len, sizeof(bProtocolHead_t)) == 0)
        {
            tmp_buf = buf + sizeof(bProtocolHead_t);
        }
        tmp_size = sizeof(bProtoFDataParam_t);
    }
    else
    {
        return 0;
    }
    ret = _bProtocolPack(attr, proto_cmd, tmp_buf, tmp_size, buf, buf_len);
    return ret;
}

/**
 * \}
 */

/**
 * \addtogroup PROTOCOL_Exported_Functions
 * \{
 */
#ifdef BSECTION_NEED_PRAGMA
#pragma section b_srv_protocol
#endif
bPROTOCOL_REG_INSTANCE("bos", _bProtocolParse, _bProtocolPackage);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif
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

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
