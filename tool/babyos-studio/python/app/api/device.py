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
# async job registry (OTA / file / xmodem)
# ---------------------------------------------------------------------------

_jobs: Dict[str, Dict[str, Any]] = {}
_jobs_lock = threading.Lock()


def _job_put(job_id: str, job: Dict[str, Any]) -> None:
    with _jobs_lock:
        _jobs[job_id] = job


def _job_get(job_id: str) -> Optional[Dict[str, Any]]:
    with _jobs_lock:
        return _jobs.get(job_id)


def _job_update(job_id: str, **fields: Any) -> None:
    with _jobs_lock:
        job = _jobs.get(job_id)
        if job is not None:
            job.update(fields)
            job["updated_at"] = time.time()


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
        _job_update(job_id, state="error", ok=False, error=str(exc),
                    finished_at=time.time())


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
        _job_update(job_id, state="error", ok=False, error=str(exc),
                    finished_at=time.time())


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
    if check_busy and dm.active_xfer is not None and getattr(
            dm.active_xfer, "is_active", False):
        raise AppError(409, "TRANSFER_BUSY", "已有传输任务在进行中")
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


class HttpProxyIn(BaseModel):
    """Host-side proxy request for mock联调 (device-side trigger is separate)."""
    url: str
    method: str = "GET"
    body: Optional[str] = None
    headers: Optional[Dict[str, str]] = None
    timeout: float = Field(default=5.0, ge=0.1, le=60.0)
    verify_tls: bool = False


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
    # Re-open is allowed: close_port cancels transfers first.
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
    resp = dm.test_link(timeout=2.0)
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
    resp = dm.set_time(utc, timeout=2.0)
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
    _require_uart()
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


@router.post("/file/stop")
def file_stop():
    dm = _dm()
    dm.stop_transfer_soft()
    # mark latest ota/file job cancelled if still running
    for kind in ("ota", "file"):
        job = _latest_job(kind)
        if job and job.get("state") in ("starting", "running"):
            _job_update(job["job_id"], state="cancelled", ok=False,
                        error="stopped by host", finished_at=time.time())
    return {"ok": True}


def _latest_job(kind: str) -> Optional[Dict[str, Any]]:
    with _jobs_lock:
        candidates = [j for j in _jobs.values() if j.get("kind") == kind]
        if not candidates:
            return None
        candidates.sort(key=lambda j: j.get("created_at", 0), reverse=True)
        return dict(candidates[0])


# ---------------------------------------------------------------------------
# Xmodem / Ymodem (async)
# ---------------------------------------------------------------------------

@router.post("/xmodem/start")
def xmodem_start(body: XferStartIn):
    _require_uart()
    if not body.path or not os.path.isfile(body.path):
        raise AppError(404, "FILE_NOT_FOUND", "文件不存在: %s" % body.path)
    dm = _dm()
    if dm.active_xfer is not None and getattr(dm.active_xfer, "is_active",
                                               False):
        raise AppError(409, "TRANSFER_BUSY", "已有传输任务在进行中")
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
    if dm.active_xfer is not None and getattr(dm.active_xfer, "is_active",
                                               False):
        raise AppError(409, "TRANSFER_BUSY", "已有传输任务在进行中")
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
    uid = dm.get_uid(timeout=2.0)
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
    text = dm.shell_command(body.cmd.strip(), timeout=body.timeout)
    return {"ok": True, "cmd": body.cmd.strip(), "response": text}


@router.post("/param/list")
def param_list():
    """`param` — list registered parameter names (firmware b_mod_param.c)."""
    _require_uart()
    dm = _dm()
    names = dm.param_list(timeout=1.0)
    return {"ok": True, "names": names, "count": len(names)}


@router.post("/param/get")
def param_get(body: ParamGetIn):
    _require_uart()
    if not body.name or not body.name.strip():
        raise AppError(400, "INVALID_REQUEST", "name 不能为空")
    dm = _dm()
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
            status_code=body.status_code, https=bool(body.https))
    except RuntimeError as exc:
        raise AppError(409, "HTTP_MOCK_RUNNING", str(exc))
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

    url = (body.url or "").strip()
    if not url or not (url.startswith("http://") or url.startswith("https://")):
        raise AppError(400, "INVALID_REQUEST",
                       "url 必须是 http:// 或 https:// 开头的绝对地址")
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

@router.get("/status")
def device_status():
    return _dm().status()


@router.get("/logs")
def device_logs(tail: int = 100):
    return {"logs": _dm().get_logs(tail=max(0, int(tail)))}
