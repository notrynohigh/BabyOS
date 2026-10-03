"""
device API — BabyOS Studio host-side device communication endpoints.

Wires FastAPI to python/device/ services (real UART / b_protocol / shell /
HTTP mock / Xmodem-Ymodem). No stubs.

Authority:
  - origin/master:tool/README.md (BabyOS 协议说明)
  - origin/master:tool/b_protocol.py, tool/mainwindow.py
  - bos/modules/b_mod_protocol.h / b_mod_protocol.c
  - bos/modules/b_mod_param.c  ("param" shell)
  - bos/algorithm/algo_crc.c   (CRC32 口径)

Async model:
  OTA / file-transfer / Xmodem-Ymodem are state machines. POST start
  returns {accepted:true, status_url, job}; poll GET .../status.

Python 3.8 compatible.
"""
from __future__ import annotations

import os
import threading
import time
from typing import Any, Dict, List, Optional

from fastapi import APIRouter
from pydantic import BaseModel, Field

from device.device_manager import get_device_manager
from device.sn_util import sn_bytes as _sn_bytes
from device.xmodem_ydmodem import XferState
from ..deps import AppError

router = APIRouter(prefix="/api/device", tags=["device"])


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _dm():
    return get_device_manager()


def _require_uart() -> None:
    dm = _dm()
    if not dm.is_open():
        raise AppError(409, "SERIAL_NOT_OPEN", "串口未打开，请先打开串口")


def _require_no_transfer(dm, what: str) -> None:
    """Shell/param/poll must not share UART with an active transfer."""
    pc = dm.protocol_client
    if pc is not None and getattr(pc, "transfer_active", False):
        raise AppError(409, "TRANSFER_BUSY",
                       "传输进行中，无法执行%s" % what)
    if dm.active_xfer is not None and getattr(dm.active_xfer, "is_active", False):
        raise AppError(409, "TRANSFER_BUSY",
                       "传输进行中，无法执行%s" % what)


def _hex(data: Optional[bytes]) -> str:
    if not data:
        return ""
    return data.hex()


def _xfer_state_name(sender: Any) -> str:
    if sender is None:
        return "idle"
    try:
        st = sender.state
        return XferState(st).name if isinstance(st, int) else str(st)
    except Exception:
        return str(getattr(sender, "state", "unknown"))


def _xfer_progress(sender: Any) -> int:
    """Best-effort progress 0..100 from xmodem/ymodem sender internals."""
    if sender is None:
        return 0
    total = getattr(sender, "_total_data_blocks", 0) or 0
    if total <= 0:
        return 0 if getattr(sender, "is_active", False) else 100
    block = getattr(sender, "_block_num", 1) or 1
    pct = int((block - 1) * 100 / total)
    return max(0, min(99, pct))


# ---------------------------------------------------------------------------
# async job registry (OTA / file / xmodem) — stored on DeviceManager


def _job_put(job_id: str, job: Dict[str, Any]) -> None:
    _dm().job_put(job_id, job)


def _job_get(job_id: str) -> Optional[Dict[str, Any]]:
    return _dm().job_get(job_id)


def _job_update(job_id: str, **fields: Any) -> None:
    _dm().job_update(job_id, **fields)


def _new_job_id(kind: str) -> str:
    return "%s-%d" % (kind, int(time.time() * 1000) & 0xFFFFFFFF)


def _run_ota_job(job_id: str, path: str, name: Optional[str],
                 timeout: float) -> None:
    dm = _dm()
    pc = dm.protocol_client
    _job_update(job_id, state="running", started_at=time.time(),
                path=path, name=name or os.path.basename(path))
    if pc is not None:
        pc.on_progress = lambda pct: _job_update(job_id, progress=int(pct))
        pc.on_result = lambda ok, code: _job_update(
            job_id, ok=bool(ok), result_code=int(code))
    try:
        ok = dm.start_ota(path, name=name, timeout=timeout)
        result = pc.transfer_result if pc is not None else None
        final_progress = 100 if ok else (_job_get(job_id) or {}).get("progress", 0)
        _job_update(
            job_id,
            state="done" if ok else "error",
            ok=bool(ok),
            result_code=result,
            finished_at=time.time(),
            progress=final_progress,
            error="" if ok else "OTA failed (result=%s)" % result,
        )
    except Exception as exc:
        result = pc.transfer_result if pc is not None else None
        if result is None and not str(exc).lower().startswith("ota"):
            # Link broken / IO error — structured timeout result, not a hang.
            from ..device.protocol_client import OTA_RESULT_TIMEOUT
            result = OTA_RESULT_TIMEOUT
        _job_update(job_id, state="error", ok=False, error=str(exc),
                    result_code=result, finished_at=time.time())


def _run_file_job(job_id: str, path: str, dev_no: int, offset: int,
                  timeout: float) -> None:
    dm = _dm()
    pc = dm.protocol_client
    _job_update(job_id, state="running", started_at=time.time(),
                path=path, dev_no=dev_no, offset=offset,
                name=os.path.basename(path))
    if pc is not None:
        pc.on_progress = lambda pct: _job_update(job_id, progress=int(pct))
        pc.on_result = lambda ok, code: _job_update(
            job_id, ok=bool(ok), result_code=int(code))
    try:
        ok = dm.start_file_transfer(path, dev_no=dev_no, offset=offset,
                                    timeout=timeout)
        result = pc.transfer_result if pc is not None else None
        final_progress = 100 if ok else (_job_get(job_id) or {}).get("progress", 0)
        _job_update(
            job_id,
            state="done" if ok else "error",
            ok=bool(ok),
            result_code=result,
            finished_at=time.time(),
            progress=final_progress,
            error="" if ok else "file transfer failed (result=%s)" % result,
        )
    except Exception as exc:
        result = pc.transfer_result if pc is not None else None
        if result is None:
            from ..device.protocol_client import OTA_RESULT_TIMEOUT
            result = OTA_RESULT_TIMEOUT
        _job_update(job_id, state="error", ok=False, error=str(exc),
                    result_code=result, finished_at=time.time())


def _run_xmodem_pump(job_id: str, kind: str) -> None:
    """Drive UART bytes into the active xmodem/ymodem sender until done."""
    dm = _dm()
    _job_update(job_id, state="running", started_at=time.time())
    try:
        while True:
            active = dm.pump_xmodem()
            sender = dm.xmodem_sender if kind == "xmodem" else dm.ymodem_sender
            if sender is not None:
                _job_update(job_id, progress=_xfer_progress(sender),
                            xfer_state=_xfer_state_name(sender))
            if not active:
                break
            time.sleep(0.01)
        sender = dm.xmodem_sender if kind == "xmodem" else dm.ymodem_sender
        state = _xfer_state_name(sender)
        ok = state in ("DONE", "XferState.DONE")
        _job_update(
            job_id,
            state="done" if ok else ("cancelled" if state == "ABORTED"
                                      else "error"),
            ok=ok,
            xfer_state=state,
            finished_at=time.time(),
            error="" if ok else ("xfer state=%s" % state),
        )
        if kind == "xmodem":
            dm.xmodem_sender = None
        else:
            dm.ymodem_sender = None
        if dm.active_xfer is sender:
            dm.active_xfer = None
    except Exception as exc:
        _job_update(job_id, state="error", ok=False, error=str(exc),
                    finished_at=time.time())


def _start_transfer_job(kind: str, runner, check_busy: bool = True,
                        **meta: Any) -> Dict[str, Any]:
    dm = _dm()
    if check_busy:
        if dm.active_xfer is not None and getattr(
                dm.active_xfer, "is_active", False):
            raise AppError(409, "TRANSFER_BUSY", "已有传输任务在进行中")
        # OTA / file transfer uses ProtocolClient._xfer_busy — reject overlap
        pc = dm.protocol_client
        if pc is not None and getattr(pc, "transfer_active", False):
            raise AppError(409, "TRANSFER_BUSY",
                           "协议传输任务进行中，请先停止或等待完成")
    job_id = _new_job_id(kind)
    job = {
        "job_id": job_id,
        "kind": kind,
        "state": "starting",
        "progress": 0,
        "ok": False,
        "result_code": None,
        "error": "",
        "created_at": time.time(),
        "updated_at": time.time(),
    }
    job.update(meta)
    _job_put(job_id, job)
    t = threading.Thread(target=runner, args=(job_id,),
                         name="device-%s" % kind, daemon=True)
    t.start()
    return {
        "accepted": True,
        "job_id": job_id,
        "status": job,
        "status_url": "/api/device/%s/status" % (
            "ota" if kind == "ota" else
            "file" if kind == "file" else "xmodem"),
        # file jobs are also readable via /ota/status?job_id=... (shared registry)
    }


# ---------------------------------------------------------------------------
# request models
# ---------------------------------------------------------------------------

class SerialOpenIn(BaseModel):
    path: str
    baud: int = 115200
    encrypt: bool = False


class SetTimeIn(BaseModel):
    utc: Optional[int] = None


class OtaStartIn(BaseModel):
    path: str
    name: Optional[str] = None
    timeout: float = Field(default=30.0, ge=0.1, le=600.0)


class XferStartIn(BaseModel):
    path: str


class FileStartIn(BaseModel):
    path: str
    dev_no: int = 0
    offset: int = 0
    timeout: float = Field(default=30.0, ge=0.1, le=600.0)


class SnWriteIn(BaseModel):
    orval: int = 0
    uid_hex: Optional[str] = None


class ShellCmdIn(BaseModel):
    cmd: str
    timeout: float = Field(default=1.0, ge=0.05, le=30.0)


class ParamGetIn(BaseModel):
    name: str
    timeout: float = Field(default=1.0, ge=0.05, le=30.0)


class ParamSetIn(BaseModel):
    name: str
    value: Any
    timeout: float = Field(default=1.0, ge=0.05, le=30.0)
    verify: bool = True


class HttpStartIn(BaseModel):
    port: int = 0
    body: Any = '{"ok":true}'
    content_type: str = "application/json"
    status_code: int = 200
    https: bool = False
    file_log: bool = True


class HttpProxyIn(BaseModel):
    """Host-side proxy request for mock联调 (device-side trigger is separate)."""
    url: str
    method: str = "GET"
    body: Optional[str] = None
    headers: Optional[Dict[str, str]] = None
    timeout: float = Field(default=5.0, ge=0.1, le=60.0)
    verify_tls: bool = False


class FileMergeFolderIn(BaseModel):
    """Folder merge → allfile.bin (CMD 0x6 prep, origin/dev _load_folder)."""
    folder_path: str
    out_name: str = "allfile.bin"


class FileTransferStopIn(BaseModel):
    """Stop 0x6 transfer. notify_device=True sends all-zero CMD_TRANS_FILE."""
    notify_device: bool = True


class NetSetCfgnetIn(BaseModel):
    """CMD 0x30 — cfg_type 0=AP 1=BLE; ssid 32B, passwd 64B on wire."""
    cfg_type: int = 0
    ssid: str = ""
    passwd: str = ""


class VoiceSetSwitchIn(BaseModel):
    on: int = 1


class VoiceSetVolumeIn(BaseModel):
    volume: int = Field(default=50, ge=0, le=100)


class VoiceTtsIn(BaseModel):
    content: str
    timeout: float = Field(default=2.0, ge=0.1, le=30.0)


class TslInvokeIn(BaseModel):
    content: str
    timeout: float = Field(default=2.0, ge=0.1, le=30.0)


class HttpDeviceRequestIn(BaseModel):
    """CMD 0x50 — device-side HTTP request (not host proxy)."""
    method: str = "GET"
    url: str
    headers: Optional[Dict[str, str]] = None
    body: Optional[str] = None
    timeout: float = Field(default=5.0, ge=0.1, le=60.0)


class ParamPollStartIn(BaseModel):
    name: str
    interval_ms: int = Field(default=1000, ge=100, le=3600000)


class LogFileStartIn(BaseModel):
    path: str


class WebConfigActionIn(BaseModel):
    project_dir: Optional[str] = None
    keil_uv4: Optional[str] = None
    openocd_dir: Optional[str] = None
    project_file_rel: Optional[str] = None
    target_name: Optional[str] = None
    log_dir_rel: Optional[str] = None
    serial_port: Optional[str] = None
    serial_baud: Optional[int] = None
    log_seconds: Optional[int] = Field(default=None, ge=1, le=600)
    save: bool = True


class WebConfigBuildIn(WebConfigActionIn):
    timeout_sec: int = Field(default=300, ge=10, le=1800)


class WebConfigFlashIn(WebConfigActionIn):
    timeout_sec: int = Field(default=120, ge=10, le=600)


class WebConfigLogIn(WebConfigActionIn):
    out_path: Optional[str] = None


# ---------------------------------------------------------------------------
# serial
# ---------------------------------------------------------------------------

@router.get("/serial/ports")
def serial_ports():
    dm = _dm()
    return {
        "ports": dm.list_ports(),
        "open": dm.is_open(),
        "current": dm.uart.port if dm.is_open() else "",
        "baudrate": dm.uart.baudrate if dm.is_open() else 0,
    }


@router.post("/serial/open")
def serial_open(body: SerialOpenIn):
    dm = _dm()
    if not body.path:
        raise AppError(400, "INVALID_REQUEST", "path 不能为空")
    if body.baud <= 0:
        raise AppError(400, "INVALID_REQUEST", "baud 必须为正整数")
    # Re-open cancels in-flight transfers/polling first (open_port does this).
    ok = dm.open_port(body.path, body.baud, encrypt=body.encrypt)
    if not ok:
        raise AppError(500, "PORT_OPEN_FAILED",
                       "串口打开失败: %s @%s" % (body.path, body.baud))
    return {
        "ok": True,
        "port": dm.uart.port,
        "baudrate": dm.uart.baudrate,
        "encrypt": bool(body.encrypt),
        "host_id": dm.protocol_client.host_id if dm.protocol_client else None,
    }


@router.post("/serial/close")
def serial_close():
    dm = _dm()
    was_open = dm.is_open()
    dm.close_port()
    return {"ok": True, "was_open": was_open, "open": dm.is_open()}


# ---------------------------------------------------------------------------
# protocol commands
# ---------------------------------------------------------------------------

@router.post("/protocol/test")
def protocol_test():
    """CMD 0x1 test("BabyOS") — device ACK with same cmd."""
    _require_uart()
    dm = _dm()
    try:
        resp = dm.test_link(timeout=2.0)
    except (IOError, OSError, RuntimeError) as exc:
        raise AppError(504, "PROTOCOL_TIMEOUT", "协议测试失败: %s" % exc)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "协议测试超时，无设备响应")
    device_id, cmd, param = resp
    return {
        "ok": True,
        "device_id": device_id,
        "cmd": cmd,
        "param_hex": _hex(param),
        "param_text": param.decode("utf-8", errors="replace"),
    }


@router.post("/protocol/set_time")
def protocol_set_time(body: SetTimeIn):
    """CMD 0x2 UTC — 4B LE Unix timestamp."""
    _require_uart()
    dm = _dm()
    utc = body.utc if body.utc is not None else int(time.time())
    try:
        resp = dm.set_time(utc, timeout=2.0)
    except (IOError, OSError, RuntimeError) as exc:
        raise AppError(504, "PROTOCOL_TIMEOUT", "设置时间失败: %s" % exc)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "设置时间超时，无设备响应")
    device_id, cmd, param = resp
    return {"ok": True, "utc": utc, "device_id": device_id, "cmd": cmd}


# ---------------------------------------------------------------------------
# OTA / file transfer (async)
# ---------------------------------------------------------------------------

@router.post("/ota/start")
def ota_start(body: OtaStartIn):
    _require_uart()
    dm = _dm()
    # param poll shells over the same UART — stop it before binary OTA
    if dm.param_polling_status().get("enabled"):
        dm.stop_param_polling()
    if not body.path or not os.path.isfile(body.path):
        raise AppError(404, "FILE_NOT_FOUND",
                       "固件文件不存在: %s" % body.path)
    if os.path.getsize(body.path) <= 0:
        raise AppError(400, "INVALID_REQUEST", "固件文件为空")
    return _start_transfer_job(
        "ota",
        lambda job_id: _run_ota_job(job_id, body.path, body.name,
                                     body.timeout),
        path=body.path, name=body.name, timeout=body.timeout,
    )


@router.get("/ota/status")
def ota_status(job_id: Optional[str] = None):
    dm = _dm()
    pc = dm.protocol_client
    job = _job_get(job_id) if job_id else _latest_job("ota")
    return {
        "job": job,
        "kind": (job or {}).get("kind", "ota"),
        "transfer_active": bool(pc.transfer_active) if pc else False,
        "transfer_result": pc.transfer_result if pc else None,
        "uart_open": dm.is_open(),
    }


@router.get("/file/status")
def file_status(job_id: Optional[str] = None):
    """Poll file-transfer job (same registry as ota; kind='file')."""
    dm = _dm()
    pc = dm.protocol_client
    job = _job_get(job_id) if job_id else _latest_job("file")
    return {
        "job": job,
        "kind": (job or {}).get("kind", "file"),
        "transfer_active": bool(pc.transfer_active) if pc else False,
        "transfer_result": pc.transfer_result if pc else None,
        "uart_open": dm.is_open(),
    }


@router.post("/file/start")
def file_start(body: FileStartIn):
    """CMD 0x6 then 0x4/0x5 pump. path may be a single file or merged allfile.bin."""
    _require_uart()
    dm = _dm()
    if dm.param_polling_status().get("enabled"):
        dm.stop_param_polling()
    if not body.path or not os.path.isfile(body.path):
        raise AppError(404, "FILE_NOT_FOUND", "文件不存在: %s" % body.path)
    if os.path.getsize(body.path) <= 0:
        raise AppError(400, "INVALID_REQUEST", "文件为空")
    return _start_transfer_job(
        "file",
        lambda job_id: _run_file_job(job_id, body.path, body.dev_no,
                                      body.offset, body.timeout),
        path=body.path, dev_no=body.dev_no, offset=body.offset,
        timeout=body.timeout,
    )


@router.post("/file/merge_folder")
def file_merge_folder(body: FileMergeFolderIn):
    """Merge folder files into BabyOS allfile.bin (0xAA01/0xAA02 records)."""
    folder = (body.folder_path or "").strip()
    if not folder or not os.path.isdir(folder):
        raise AppError(404, "FOLDER_NOT_FOUND", "目录不存在: %s" % folder)
    out_name = (body.out_name or "allfile.bin").strip() or "allfile.bin"
    try:
        info = _dm().merge_folder(folder, out_name)
    except (ValueError, OSError, IOError) as exc:
        raise AppError(400, "MERGE_FAILED", str(exc))
    return {"ok": True, **info}


@router.post("/file/stop")
def file_stop(body: Optional[FileTransferStopIn] = None):
    """Soft-stop transfer; notify_device sends all-zero CMD 0x6 (dev tool)."""
    notify = True if body is None else bool(body.notify_device)
    dm = _dm()
    dm.stop_transfer(notify_device=notify)
    # mark latest ota/file job cancelled if still running
    for kind in ("ota", "file"):
        job = _latest_job(kind)
        if job and job.get("state") in ("starting", "running"):
            _job_update(job["job_id"], state="cancelled", ok=False,
                        error="stopped by host", finished_at=time.time())
    return {"ok": True, "notified_device": notify}


def _latest_job(kind: str) -> Optional[Dict[str, Any]]:
    return _dm().latest_job(kind)


# ---------------------------------------------------------------------------
# Xmodem / Ymodem (async)
# ---------------------------------------------------------------------------

@router.post("/xmodem/start")
def xmodem_start(body: XferStartIn):
    _require_uart()
    if not body.path or not os.path.isfile(body.path):
        raise AppError(404, "FILE_NOT_FOUND", "文件不存在: %s" % body.path)
    dm = _dm()
    # OTA uses pc._xfer_busy and leaves dm.active_xfer None — check both
    _require_no_transfer(dm, "Xmodem 传输")
    size = dm.load_xmodem_file(body.path)
    if size <= 0:
        raise AppError(400, "INVALID_REQUEST", "文件为空")
    if not dm.start_xmodem():
        raise AppError(409, "TRANSFER_BUSY",
                       "无法启动 Xmodem（串口未开或已有传输）")
    # start_xmodem already set active_xfer — skip re-check
    return _start_transfer_job(
        "xmodem",
        lambda job_id: _run_xmodem_pump(job_id, "xmodem"),
        check_busy=False,
        path=body.path, filename=dm.xmodem_filename, size=size,
    )


@router.post("/xmodem/cancel")
def xmodem_cancel():
    dm = _dm()
    dm.cancel_xmodem()
    job = _latest_job("xmodem")
    if job and job.get("state") in ("starting", "running"):
        _job_update(job["job_id"], state="cancelled", ok=False,
                    error="cancelled by host", finished_at=time.time())
    return {"ok": True}


@router.post("/ymodem/start")
def ymodem_start(body: XferStartIn):
    _require_uart()
    if not body.path or not os.path.isfile(body.path):
        raise AppError(404, "FILE_NOT_FOUND", "文件不存在: %s" % body.path)
    dm = _dm()
    _require_no_transfer(dm, "Ymodem 传输")
    size = dm.load_ymodem_file(body.path)
    if size <= 0:
        raise AppError(400, "INVALID_REQUEST", "文件为空")
    if not dm.start_ymodem():
        raise AppError(409, "TRANSFER_BUSY",
                       "无法启动 Ymodem（串口未开或已有传输）")
    return _start_transfer_job(
        "ymodem",
        lambda job_id: _run_xmodem_pump(job_id, "ymodem"),
        check_busy=False,
        path=body.path, filename=dm.ymodem_filename, size=size,
    )


@router.post("/ymodem/cancel")
def ymodem_cancel():
    dm = _dm()
    dm.cancel_ymodem()
    job = _latest_job("ymodem")
    if job and job.get("state") in ("starting", "running"):
        _job_update(job["job_id"], state="cancelled", ok=False,
                    error="cancelled by host", finished_at=time.time())
    return {"ok": True}


@router.get("/xmodem/status")
def xmodem_status(job_id: Optional[str] = None, kind: str = "xmodem"):
    dm = _dm()
    if kind == "ymodem":
        sender = dm.ymodem_sender
        filename = dm.ymodem_filename
    else:
        sender = dm.xmodem_sender
        filename = dm.xmodem_filename
        kind = "xmodem"
    job = _job_get(job_id) if job_id else _latest_job(kind)
    return {
        "job": job,
        "active": dm.active_xfer is not None
        and getattr(dm.active_xfer, "is_active", False),
        "kind": kind,
        "filename": filename,
        "xfer_state": _xfer_state_name(sender),
        "progress": _xfer_progress(sender),
        "uart_open": dm.is_open(),
    }


# ---------------------------------------------------------------------------
# device info: UID / SN / DEVINFO
# ---------------------------------------------------------------------------

@router.post("/uid/get")
def uid_get():
    """CMD 0x7 — device replies len(1)+uid(n)."""
    _require_uart()
    dm = _dm()
    try:
        uid = dm.get_uid(timeout=2.0)
    except (IOError, OSError, RuntimeError) as exc:
        raise AppError(504, "PROTOCOL_TIMEOUT", "获取 UID 失败: %s" % exc)
    if uid is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "获取 UID 超时，无设备响应")
    return {"ok": True, "uid_hex": _hex(uid), "uid_len": len(uid)}


@router.post("/sn/write")
def sn_write(body: SnWriteIn):
    """
    CMD 0x8 — SN = md5(uid)[:16] each byte | orval, 1-byte length prefix.
    Uses last_uid cached from /uid/get, or explicit uid_hex.
    """
    _require_uart()
    dm = _dm()
    uid = None
    if body.uid_hex:
        try:
            uid = bytes.fromhex(body.uid_hex.strip())
        except ValueError:
            raise AppError(400, "INVALID_REQUEST", "uid_hex 必须是十六进制字符串")
    if uid is None:
        uid = dm.last_uid or None
    if not uid:
        raise AppError(400, "UID_NOT_AVAILABLE",
                       "无可用 UID，请先调用 /api/device/uid/get 或传 uid_hex")
    orval = int(body.orval) & 0xFF
    # SN is computed host-side (md5(uid)[:16] each byte | orval, len prefix)
    # and sent in CMD_WRITE_SN param — device ACKs empty (firmware behavior).
    sn = _sn_bytes(uid, orval)
    resp = dm.write_sn(sn_bytes=sn, timeout=2.0)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "写入 SN 超时，无设备响应")
    device_id, cmd, param = resp
    dm.last_sn = sn
    return {
        "ok": True,
        "orval": orval,
        "device_id": device_id,
        "cmd": cmd,
        "sn_hex": _hex(sn),
        "sn_len": len(sn),
    }


@router.post("/info/get")
def info_get():
    """CMD 0xA — version(16)+name(16)."""
    _require_uart()
    dm = _dm()
    info = dm.get_device_info(timeout=2.0)
    if info is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "获取设备信息超时，无设备响应")
    version, model = info
    return {"ok": True, "version": version, "model": model}


# ---------------------------------------------------------------------------
# shell / param
# ---------------------------------------------------------------------------

@router.post("/shell/cmd")
def shell_cmd(body: ShellCmdIn):
    """Send a raw shell text command (not a b_protocol frame)."""
    _require_uart()
    if not body.cmd or not body.cmd.strip():
        raise AppError(400, "INVALID_REQUEST", "cmd 不能为空")
    dm = _dm()
    _require_no_transfer(dm, "Shell 命令")
    text = dm.shell_command(body.cmd.strip(), timeout=body.timeout)
    return {"ok": True, "cmd": body.cmd.strip(), "response": text}


@router.post("/param/list")
def param_list():
    """`param` — list registered parameter names (firmware b_mod_param.c)."""
    _require_uart()
    dm = _dm()
    _require_no_transfer(dm, "参数列表")
    names = dm.param_list(timeout=1.0)
    return {"ok": True, "names": names, "count": len(names)}


@router.post("/param/get")
def param_get(body: ParamGetIn):
    _require_uart()
    if not body.name or not body.name.strip():
        raise AppError(400, "INVALID_REQUEST", "name 不能为空")
    dm = _dm()
    _require_no_transfer(dm, "参数读取")
    value = dm.param_get(body.name.strip(), timeout=body.timeout)
    if value is None:
        raise AppError(504, "PARAM_NOT_FOUND",
                       "参数 %s 不存在或读取超时" % body.name.strip())
    return {"ok": True, "name": body.name.strip(), "value": value}


@router.post("/param/set")
def param_set(body: ParamSetIn):
    _require_uart()
    if not body.name or not body.name.strip():
        raise AppError(400, "INVALID_REQUEST", "name 不能为空")
    dm = _dm()
    _require_no_transfer(dm, "参数写入")
    ok = dm.param_set(body.name.strip(), body.value,
                      timeout=body.timeout, verify=body.verify)
    if not ok:
        raise AppError(500, "PARAM_SET_FAILED",
                       "参数 %s 设置失败（写入或校验未通过）" % body.name.strip())
    return {"ok": True, "name": body.name.strip(), "value": body.value,
            "verified": bool(body.verify)}


# ---------------------------------------------------------------------------
# HTTP mock
# ---------------------------------------------------------------------------

@router.post("/http/start")
def http_start(body: HttpStartIn):
    """Start the real local HTTP(S) mock server (records every request)."""
    dm = _dm()
    if dm.http_mock.is_running:
        raise AppError(409, "HTTP_MOCK_RUNNING",
                       "HTTP Mock 已在端口 %d 运行，请先停止" % dm.http_mock.port)
    try:
        port = dm.start_http_mock(
            port=body.port, body=body.body,
            content_type=body.content_type,
            status_code=body.status_code, https=bool(body.https),
            file_log=bool(body.file_log))
    except RuntimeError as exc:
        msg = str(exc)
        low = msg.lower()
        if ("certificate" in low or "content mismatch" in low
                or "origin/dev" in low or "sha256" in low):
            # cert missing / content mismatch — structured, never bare 500
            raise AppError(409, "HTTPS_CERT_MISMATCH", msg)
        if "already running" in low:
            raise AppError(409, "HTTP_MOCK_RUNNING", msg)
        raise AppError(409, "HTTP_MOCK_START_FAILED", msg)
    except Exception as exc:
        raise AppError(500, "HTTP_MOCK_START_FAILED", str(exc))
    return {
        "ok": True,
        "port": port,
        "base_url": dm.http_mock.base_url,
        "https": bool(body.https),
        "status": dm.http_mock.status(),
    }


@router.post("/http/stop")
def http_stop():
    dm = _dm()
    was = dm.http_mock.is_running
    dm.stop_http_mock()
    return {"ok": True, "was_running": was, "status": dm.http_mock.status()}


@router.get("/http/status")
def http_status():
    return _dm().http_mock.status()


@router.get("/http/requests")
def http_requests():
    """Recorded mock traffic (method/path/body/headers/client)."""
    dm = _dm()
    return {
        "running": dm.http_mock.is_running,
        "base_url": dm.http_mock.base_url,
        "count": dm.http_mock.request_count,
        "requests": dm.http_mock.get_requests_log(),
    }


@router.post("/http/proxy")
def http_proxy(body: HttpProxyIn):
    """
    Host-side proxy request for mock联调.

    Sends a real HTTP request FROM this machine to `url` (typically the
    mock base_url) so the mock request log fills up without needing device
    firmware HTTP client. Device-side HTTP trigger remains a firmware
    action (bHttp / project code) — not a b_protocol command.
    """
    import httpx
    from urllib.parse import urlparse

    url = (body.url or "").strip()
    # Require an absolute http(s) URL with a non-empty host.
    # Bare "http://" / scheme-only strings are invalid requests (400),
    # not downstream proxy failures (502).
    parsed = urlparse(url)
    if (not url or parsed.scheme not in ("http", "https")
            or not parsed.netloc):
        raise AppError(400, "INVALID_REQUEST",
                       "url 必须是 http:// 或 https:// 开头且包含主机的绝对地址")
    method = (body.method or "GET").upper()
    if method not in ("GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"):
        raise AppError(400, "INVALID_REQUEST", "不支持的 HTTP method: %s" % method)
    headers = body.headers or {}
    content = body.body.encode("utf-8") if body.body is not None else None
    try:
        with httpx.Client(verify=body.verify_tls, timeout=body.timeout) as client:
            resp = client.request(method, url, content=content, headers=headers)
    except Exception as exc:
        raise AppError(502, "HTTP_PROXY_FAILED",
                       "代理请求失败: %s" % exc)
    text = resp.text
    return {
        "ok": True,
        "url": url,
        "method": method,
        "status_code": resp.status_code,
        "headers": dict(resp.headers),
        "body": text[:8192],
        "body_len": len(resp.content),
    }


# ---------------------------------------------------------------------------
# aggregate status + logs
# ---------------------------------------------------------------------------

@router.post("/param/poll/start")
def param_poll_start(body: ParamPollStartIn):
    """Timed shell `param <name>` polling (origin/dev mainwindow)."""
    _require_uart()
    name = (body.name or "").strip()
    if not name:
        raise AppError(400, "INVALID_REQUEST", "name 不能为空")
    dm = _dm()
    _require_no_transfer(dm, "参数轮询")
    try:
        dm.start_param_polling(name, interval_ms=body.interval_ms)
    except ValueError as exc:
        raise AppError(400, "INVALID_REQUEST", str(exc))
    except RuntimeError as exc:
        msg = str(exc)
        if "传输" in msg:
            raise AppError(409, "TRANSFER_BUSY", msg)
        raise AppError(409, "SERIAL_NOT_OPEN", msg)
    return {"ok": True, "status": dm.param_polling_status()}


@router.post("/param/poll/stop")
def param_poll_stop():
    dm = _dm()
    was = dm.param_polling_status()["enabled"]
    dm.stop_param_polling()
    return {"ok": True, "was_enabled": was, "status": dm.param_polling_status()}


@router.get("/param/poll/status")
def param_poll_status():
    return _dm().param_polling_status()


# ---------------------------------------------------------------------------
# network / voice / TSL (device protocol 0x30–0x44 / 0x09)
# ---------------------------------------------------------------------------

@router.post("/net/set_cfgnet")
def net_set_cfgnet(body: NetSetCfgnetIn):
    """CMD 0x30 — cfg_type 0=AP 1=BLE; empty ssid = default mode on/off."""
    _require_uart()
    cfg_type = int(body.cfg_type)
    if cfg_type not in (0, 1):
        raise AppError(400, "INVALID_REQUEST", "cfg_type 只能是 0(AP) 或 1(BLE)")
    ssid = (body.ssid or "").strip()
    passwd = body.passwd or ""
    if cfg_type == 1 and not ssid:
        # BLE mode on device: firmware uses empty ssid to mean BLE toggle
        pass
    elif cfg_type == 0 and not ssid:
        raise AppError(400, "INVALID_REQUEST", "AP 模式 ssid 不能为空")
    resp = _dm().set_cfgnet_mode(cfg_type, ssid, passwd, timeout=2.0,
                                  wait_ack=True)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "设置配网模式超时")
    device_id, cmd, param = resp
    return {"ok": True, "cfg_type": cfg_type, "ssid": ssid,
            "device_id": device_id, "cmd": cmd}


@router.post("/net/get_info")
def net_get_info():
    """CMD 0x31 — ssid(32)+ip+gw+mask → dotted IPs."""
    _require_uart()
    info = _dm().get_netinfo(timeout=2.0)
    if info is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "获取网络信息超时")
    return {"ok": True, **info}


@router.post("/voice/set_switch")
def voice_set_switch(body: VoiceSetSwitchIn):
    """CMD 0x40 — 0=off 1=on."""
    _require_uart()
    on = 1 if int(body.on) else 0
    resp = _dm().set_voice_switch(on, timeout=2.0, wait_ack=True)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "设置语音开关超时")
    device_id, cmd, param = resp
    return {"ok": True, "on": on, "device_id": device_id, "cmd": cmd}


@router.post("/voice/set_volume")
def voice_set_volume(body: VoiceSetVolumeIn):
    """CMD 0x41 — 0~100."""
    _require_uart()
    volume = max(0, min(100, int(body.volume)))
    resp = _dm().set_voice_volume(volume, timeout=2.0, wait_ack=True)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "设置音量超时")
    device_id, cmd, param = resp
    return {"ok": True, "volume": volume, "device_id": device_id, "cmd": cmd}


@router.post("/voice/get_volume")
def voice_get_volume():
    """CMD 0x42 — 1-byte reply."""
    _require_uart()
    vol = _dm().get_voice_volume(timeout=2.0)
    if vol is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "获取音量超时")
    return {"ok": True, "volume": int(vol)}


@router.post("/voice/get_stat")
def voice_get_stat():
    """CMD 0x43 — 0=idle 1=listening 2=playing."""
    _require_uart()
    st = _dm().get_voice_stat(timeout=2.0)
    if st is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "获取语音状态超时")
    return {"ok": True, **st}


@router.post("/voice/tts")
def voice_tts(body: VoiceTtsIn):
    """CMD 0x44 — TTS content for device playback."""
    _require_uart()
    content = body.content or ""
    if not content.strip():
        raise AppError(400, "INVALID_REQUEST", "content 不能为空")
    resp = _dm().send_tts_content(content, timeout=body.timeout, wait_ack=True)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "TTS 内容发送超时")
    device_id, cmd, param = resp
    return {"ok": True, "device_id": device_id, "cmd": cmd,
            "content": content}


@router.post("/tsl/invoke")
def tsl_invoke(body: TslInvokeIn):
    """CMD 0x09 — 物模型方法调用内容."""
    _require_uart()
    content = body.content or ""
    if not content.strip():
        raise AppError(400, "INVALID_REQUEST", "content 不能为空")
    resp = _dm().invoke_tsl(content, timeout=body.timeout)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "物模型调用超时")
    device_id, cmd, param = resp
    return {"ok": True, "device_id": device_id, "cmd": cmd,
            "param_hex": _hex(param)}


# ---------------------------------------------------------------------------
# HTTP via protocol (CMD 0x50–0x53) — device-side request, not host proxy
# ---------------------------------------------------------------------------

@router.post("/http/init")
def http_device_init():
    """CMD 0x52 — device HTTP client init."""
    _require_uart()
    resp = _dm().http_init(timeout=2.0)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "HTTP init 超时")
    device_id, cmd, param = resp
    return {"ok": True, "device_id": device_id, "cmd": cmd}


@router.post("/http/deinit")
def http_device_deinit():
    """CMD 0x53 — device HTTP client deinit."""
    _require_uart()
    resp = _dm().http_deinit(timeout=2.0)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "HTTP deinit 超时")
    device_id, cmd, param = resp
    return {"ok": True, "device_id": device_id, "cmd": cmd}


@router.post("/http/request")
def http_device_request(body: HttpDeviceRequestIn):
    """CMD 0x50 → device builds request → 0x51 response (status+body)."""
    _require_uart()
    url = (body.url or "").strip()
    if not url:
        raise AppError(400, "INVALID_REQUEST", "url 不能为空")
    method = (body.method or "GET").upper()
    if method not in ("GET", "POST", "PUT", "DELETE"):
        raise AppError(400, "INVALID_REQUEST", "不支持的 HTTP method: %s" % method)
    headers = body.headers or {}
    header_lines = []
    for k, v in headers.items():
        header_lines.append("%s: %s" % (k, v))
    header_text = "\r\n".join(header_lines)
    raw_body = body.body.encode("utf-8") if body.body is not None else b""
    resp = _dm().http_request(method, url, header_text, raw_body,
                              timeout=body.timeout)
    if resp is None:
        raise AppError(504, "PROTOCOL_TIMEOUT", "设备 HTTP 请求超时")
    return {"ok": True, **resp}


# ---------------------------------------------------------------------------
# UART log-to-file
# ---------------------------------------------------------------------------

@router.post("/log/start")
def log_start(body: LogFileStartIn):
    """Append all DeviceManager log lines to disk (dev tool log-to-file)."""
    path = (body.path or "").strip()
    if not path:
        raise AppError(400, "INVALID_REQUEST", "path 不能为空")
    try:
        out = _dm().start_log_to_file(path)
    except (ValueError, IOError, OSError) as exc:
        raise AppError(400, "LOG_START_FAILED", str(exc))
    return {"ok": True, "path": out}


@router.post("/log/stop")
def log_stop():
    dm = _dm()
    status = dm.log_to_file_status()
    dm.stop_log_to_file()
    return {"ok": True, "was": status, "status": dm.log_to_file_status()}


@router.get("/log/status")
def log_status():
    return _dm().log_to_file_status()


# ---------------------------------------------------------------------------
# 配网 Web host tooling (Keil / OpenOCD / pyserial)
# ---------------------------------------------------------------------------

def _wc_apply(body: WebConfigActionIn):
    """Apply webconfig path/port fields.

    - None  = leave unchanged
    - ""    = clear the field (only honored when body.save is True)
    - other = set
    """
    updates = {}
    clearable = ("project_dir", "keil_uv4", "openocd_dir",
                 "project_file_rel", "target_name", "log_dir_rel",
                 "serial_port")
    for key in clearable + ("serial_baud", "log_seconds"):
        val = getattr(body, key, None)
        if val is None:
            continue
        if isinstance(val, str) and val == "" and key in clearable:
            updates[key] = ""
        elif val != "":
            updates[key] = val
    if updates and body.save:
        try:
            _dm().webconfig.save_config(updates)
        except (OSError, IOError) as exc:
            raise AppError(500, "WEBCONFIG_CONFIG_FAILED", str(exc))
    return updates


@router.get("/webconfig/status")
def webconfig_status():
    return _dm().webconfig.status()


@router.post("/webconfig/status")
def webconfig_set(body: WebConfigActionIn):
    updates = _wc_apply(body)
    return {"ok": True, "updated": updates, "status": _dm().webconfig.status()}


@router.post("/webconfig/build")
def webconfig_build(body: WebConfigBuildIn):
    _wc_apply(body)
    wc = _dm().webconfig
    result = wc.build(timeout_sec=int(body.timeout_sec))
    if not result.get("ok"):
        raise AppError(400, "WEBCONFIG_BUILD_FAILED",
                       result.get("error") or "build failed",
                       extra={"logs": result.get("logs") or []})
    return {"ok": True, **result}


@router.post("/webconfig/flash")
def webconfig_flash(body: WebConfigFlashIn):
    _wc_apply(body)
    wc = _dm().webconfig
    result = wc.flash(timeout_sec=int(body.timeout_sec))
    if not result.get("ok"):
        raise AppError(400, "WEBCONFIG_FLASH_FAILED",
                       result.get("error") or "flash failed",
                       extra={"logs": result.get("logs") or []})
    return {"ok": True, **result}


@router.post("/webconfig/log/start")
def webconfig_log_start(body: WebConfigLogIn):
    _wc_apply(body)
    wc = _dm().webconfig
    result = wc.start_log(port=body.serial_port,
                          baud=body.serial_baud,
                          seconds=body.log_seconds,
                          out_path=body.out_path)
    if not result.get("ok"):
        raise AppError(400, "WEBCONFIG_LOG_FAILED",
                       result.get("error") or "log start failed",
                       extra={"logs": result.get("logs") or []})
    return {"ok": True, **result}


@router.post("/webconfig/log/stop")
def webconfig_log_stop():
    return _dm().webconfig.stop_log()


@router.get("/webconfig/log/status")
def webconfig_log_status():
    return _dm().webconfig.log_status()


# ---------------------------------------------------------------------------
# aggregate status + logs
# ---------------------------------------------------------------------------

@router.get("/status")
def device_status():
    return _dm().status()


@router.get("/logs")
def device_logs(tail: int = 100):
    return {"logs": _dm().get_logs(tail=max(0, int(tail)))}
