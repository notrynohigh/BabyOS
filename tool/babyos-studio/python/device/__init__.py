"""
device — BabyOS Studio host-side device communication package.

Real protocol stack ported from origin/master:tool/ and cross-checked against
firmware bos/modules/b_mod_protocol.c and bos/algorithm/algo_crc.c.

Modules:
  b_protocol         — frame pack/parse + TEA encrypt/decrypt + command payloads
  crc_util           — CRC32 matching algo_crc.c ALGO_CRC32
  sn_util            — md5_hex_16 + sn_bytes(uid, orval)
  uart_service       — pyserial wrapper (list/open/close/write/read_available)
  xmodem_ydmodem     — Xmodem-128 / Ymodem-1K senders
  protocol_client    — ProtocolClient host business API (test/time/uid/sn/devinfo/OTA)
  shell_client       — ShellClient "param ..." text commands
  http_mock          — real ThreadingHTTPServer HTTP mock with request log
  device_manager     — singleton DeviceManager for FastAPI
"""

from .b_protocol import (
    PROTOCOL_HEAD,
    INVALID_ID,
    DEVICE_ID_HOST,
    CMD_TEST,
    CMD_UTC,
    CMD_FW_INFO,
    CMD_FDATA,
    CMD_OTA_RESULT,
    CMD_TRANS_FILE,
    CMD_GET_UID,
    CMD_WRITE_SN,
    CMD_TSL_INVOKE,
    CMD_DEVICEINFO,
    OTA_RESULT_OK,
    OTA_RESULT_CRC_ERROR,
    OTA_RESULT_NAME_MISMATCH,
    OTA_RESULT_LEN_INVALID,
    OTA_RESULT_TIMEOUT,
    TEA_KEY,
    TEA_DELTA,
    pack_frame,
    parse_frame,
    calc_checksum,
    tea_encrypt,
    tea_decrypt,
    bProtocolEncrypt,
    bProtocolDecrypt,
    bProtocolRegist,
    bProtocolSetID,
    bProtocolParse,
    bProtocolPack,
    bProtocolPackBytes,
    build_test_param,
    build_utc_param,
    build_fw_info_param,
    parse_fw_info_param,
    build_fdata_req_param,
    parse_fdata_req_param,
    build_fdata_param,
    parse_fdata_param,
    build_ota_result_param,
    parse_ota_result_param,
    build_trans_file_param,
    parse_trans_file_param,
    parse_uid_response,
    build_sn_param,
    parse_devinfo_response,
    build_devinfo_param,
)

from .crc_util import (
    crc32,
    crc32_d,
    crc32_equals_zlib,
    crc32_sbs_start,
    crc32_sbs_update,
    crc16_xmodem,
    crc_calculate,
    ALGO_CRC32,
    CRC32_SPEC,
)

from .sn_util import (
    md5_hex_16,
    sn_bytes,
    sn_hex,
    sn_body,
    SN_DIGEST_LEN,
)

from .uart_service import (
    UartService,
    get_uart_service,
)

from .xmodem_ydmodem import (
    XmodemSender,
    YmodemSender,
    crc16_ccitt,
    crc16_ymodem,
    CRC16_YMODEM_VECTORS,
    XferState,
    SOH,
    STX,
    EOT,
    ACK,
    NAK,
    CAN,
    CRCPKT,
)

from .protocol_client import (
    ProtocolClient,
)

from .shell_client import (
    ShellClient,
)

from .http_mock import (
    HttpMock,
)

from .device_manager import (
    DeviceManager,
    get_device_manager,
)

__all__ = [
    # protocol
    'PROTOCOL_HEAD', 'INVALID_ID', 'DEVICE_ID_HOST',
    'CMD_TEST', 'CMD_UTC', 'CMD_FW_INFO', 'CMD_FDATA', 'CMD_OTA_RESULT',
    'CMD_TRANS_FILE', 'CMD_GET_UID', 'CMD_WRITE_SN', 'CMD_TSL_INVOKE',
    'CMD_DEVICEINFO',
    'OTA_RESULT_OK', 'OTA_RESULT_CRC_ERROR', 'OTA_RESULT_NAME_MISMATCH',
    'OTA_RESULT_LEN_INVALID', 'OTA_RESULT_TIMEOUT',
    'TEA_KEY', 'TEA_DELTA',
    'pack_frame', 'parse_frame', 'calc_checksum',
    'tea_encrypt', 'tea_decrypt', 'bProtocolEncrypt', 'bProtocolDecrypt',
    'bProtocolRegist', 'bProtocolSetID', 'bProtocolParse', 'bProtocolPack',
    'bProtocolPackBytes',
    'build_test_param', 'build_utc_param',
    'build_fw_info_param', 'parse_fw_info_param',
    'build_fdata_req_param', 'parse_fdata_req_param',
    'build_fdata_param', 'parse_fdata_param',
    'build_ota_result_param', 'parse_ota_result_param',
    'build_trans_file_param', 'parse_trans_file_param',
    'parse_uid_response', 'build_sn_param',
    'parse_devinfo_response', 'build_devinfo_param',
    # crc
    'crc32', 'crc32_d', 'crc32_equals_zlib', 'crc32_sbs_start',
    'crc32_sbs_update', 'crc16_xmodem', 'crc_calculate',
    'ALGO_CRC32', 'CRC32_SPEC',
    # sn
    'md5_hex_16', 'sn_bytes', 'sn_hex', 'sn_body', 'SN_DIGEST_LEN',
    # uart
    'UartService', 'get_uart_service',
    # xmodem
    'XmodemSender', 'YmodemSender', 'crc16_ccitt', 'crc16_ymodem',
    'CRC16_YMODEM_VECTORS', 'XferState',
    'SOH', 'STX', 'EOT', 'ACK', 'NAK', 'CAN', 'CRCPKT',
    # business services
    'ProtocolClient', 'ShellClient', 'HttpMock',
    'DeviceManager', 'get_device_manager',
]
