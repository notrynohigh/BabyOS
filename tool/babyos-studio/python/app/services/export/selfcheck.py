"""导出自检（FR-8.5，design §7.4 五步）：
①编译 ②predict 一致性 ③特征链 ④静态扫描 ⑤符号映射校验 + example 编译。

任一硬性步骤失败 → 导出 422 拦截（export_service 转 AppError）。
"""
from __future__ import annotations

import ctypes
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

from . import reference

BANNED_RE = re.compile(r"\b(malloc|free|calloc|realloc|printf)\b")

MODEL_C_FILES = ("algo_",)

# 必为 bundle 之外、不应出现的 bAlgoMl* 定义正则（粗糙辅证；主判据见 nm）。
_BANNED_DEF_RE = re.compile(r"^\s*(?:static\s+)?(?:float|int|void|uint\d+_t|int\d+_t|double)"
                            r"\s+bAlgoMl\w+\s*\([^;]*\)\s*\{")


def step_compile(workdir: Path, repo_root: Path | None = None) -> dict:
    """① gcc -std=c99 -Wall -Wextra -Werror -ffp-contract=off 编译为 .so。

    自检锚点：编译清单包含**仓库真实 algo_ml.c**（非 bundle 复制），保证生成代码与
    模块契约同源。example 文件独立 compile 一遍（见 step_symbol_map）。
    深度绑定（2026-10-01）：同时注入 algo_signal.c 和 algo_fft.c（如果导出代码使用了这些原语）。
    """
    from .c_common import STUB_B_OS_H, find_babyos_root

    root = repo_root or find_babyos_root()
    algo_ml_c = root / "bos" / "algorithm" / "algo_ml.c"
    if not algo_ml_c.is_file():
        return {"ok": False, "compiler": "",
                "stderr": f"仓库 algo_ml.c 不存在: {algo_ml_c}"}

    # 检查是否需要注入 algo_signal.c 和 algo_fft.c
    algo_signal_c = root / "bos" / "algorithm" / "algo_signal.c"
    algo_fft_c = root / "bos" / "algorithm" / "algo_fft.c"

    stub = workdir / "stub"
    stub.mkdir(exist_ok=True)
    (stub / "b_os.h").write_text(STUB_B_OS_H, encoding="utf-8")
    c_files = [str(p) for p in sorted(workdir.rglob("*.c"))
               if "example" not in p.parts and "stub" not in p.parts
               and p.name != "main.c"]  # 评审 B1：main.c 是 MCU 工程模板，host 自检跳过
    # 例外：example 单独处理；仓库 algo_ml.c 注入以保证符号契约同源
    c_files = [p for p in c_files if not p.endswith("/algo_ml_common.c")]
    c_files.append(str(algo_ml_c))

    # 深度绑定：注入 algo_signal.c 和 algo_fft.c（如果存在且导出代码使用了这些原语）
    compile_defs = ["-D_ALGO_ML_ENABLE=1"]
    if algo_signal_c.is_file():
        # 检查导出代码是否使用了 algo_signal.h
        uses_signal = any(
            "algo_signal.h" in p.read_text(encoding="utf-8", errors="ignore")
            for p in workdir.rglob("*.c")
            if "example" not in p.parts and p.name != "main.c"
        )
        if uses_signal:
            c_files.append(str(algo_signal_c))
            compile_defs.append("-D_ALGO_SIGNAL_ENABLE=1")

    if algo_fft_c.is_file():
        # 检查导出代码是否使用了 algo_fft.h
        uses_fft = any(
            "algo_fft.h" in p.read_text(encoding="utf-8", errors="ignore")
            for p in workdir.rglob("*.c")
            if "example" not in p.parts and p.name != "main.c"
        )
        if uses_fft:
            c_files.append(str(algo_fft_c))
            compile_defs.append("-D_ALGO_FFT_ENABLE=1")

    so = workdir / "_selfcheck.so"
    cmd = ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off",
           "-shared", "-fPIC", "-O2",
           "-I", str(stub), "-I", str(workdir),
           "-I", str(root / "bos" / "algorithm"),
           "-I", str(root / "bos" / "algorithm" / "inc"),
           ] + compile_defs + ["-lm", "-o", str(so)] + c_files
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    ver = subprocess.run(["gcc", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    if r.returncode != 0:
        return {"ok": False, "compiler": ver, "stderr": r.stderr[-4000:]}
    return {"ok": True, "compiler": ver, "so": str(so), "repo_root": str(root)}


def step_predict_consistency(workdir: Path, symbol: str, payload: dict,
                             X_tr: np.ndarray, X_te: np.ndarray) -> dict:
    """② ctypes 调 C predict vs float32 参考实现：类别 id 100% 一致；proba 容差 2e-2。"""
    return _predict_consistency_impl(str(workdir / "_selfcheck.so"), symbol, payload, X_tr, X_te)


def _predict_consistency_impl(so_path: str, symbol: str, payload: dict,
                              X_tr: np.ndarray, X_te: np.ndarray) -> dict:
    lib = ctypes.CDLL(so_path)
    try:
        fn = getattr(lib, f"algo_{symbol}_predict")
        fn.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_uint32, ctypes.POINTER(ctypes.c_float)]
        fn.restype = ctypes.c_int
        nc = len(payload["labels"])

        ref_proba = reference.predict_proba_ref(payload, np.vstack([X_tr, X_te]))
        ref_id = np.argmax(ref_proba, axis=1)

        X_all = np.vstack([X_tr, X_te]).astype(np.float32)
        c_id = np.empty(len(X_all), dtype=np.int64)
        c_proba = np.empty((len(X_all), nc), dtype=np.float32)
        for i in range(len(X_all)):
            xin = np.ascontiguousarray(X_all[i], dtype=np.float32)
            pout = np.empty(nc, dtype=np.float32)
            r = fn(xin.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), xin.shape[0],
                   pout.ctypes.data_as(ctypes.POINTER(ctypes.c_float)))
            if r < 0:
                return {"ok": False, "error": f"sample {i}: C predict 返回 {r}"}
            c_id[i] = r
            c_proba[i] = pout

        id_match = float((c_id == ref_id).mean())
        max_proba_diff = float(np.max(np.abs(c_proba - ref_proba))) if len(X_all) else 0.0
        ok = id_match == 1.0 and max_proba_diff <= 2e-2
        return {
            "ok": ok,
            "n_samples": int(len(X_all)),
            "id_match_rate": id_match,
            "max_proba_diff": max_proba_diff,
            "proba_tol": 2e-2,
        }
    finally:
        # Windows: ctypes.CDLL 持锁 .so 文件，须显式 FreeLibrary 才能 rmtree
        if hasattr(lib, "_handle") and sys.platform == "win32":
            try:
                ctypes.windll.kernel32.FreeLibrary(lib._handle)
            except Exception:  # noqa: BLE001 - 释放失败不阻塞结果
                pass


def step_feature_chain(workdir: Path, symbol: str, windows: np.ndarray,
                       expected: np.ndarray, win_len: int, n_ch: int) -> dict:
    """③ 特征链一致性：原始 N 点窗口 → C feat_extract vs X[:, feature_indices]。

    组合容差 |a-b| ≤ max(1e-3·|b|, 5e-6)（近零特征如 zcr/频带比不误爆）。
    windows: (n_win, n_ch*N) float32 通道分离；expected: (n_win, n_feat) float32。
    """
    lib = ctypes.CDLL(str(workdir / "_selfcheck.so"))
    try:
        fn = getattr(lib, f"algo_{symbol}_feat_extract")
        fn.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_uint32, ctypes.POINTER(ctypes.c_float)]
        fn.restype = ctypes.c_int

        n_win, nf = expected.shape
        got = np.empty((n_win, nf), dtype=np.float32)
        for i in range(n_win):
            w = np.ascontiguousarray(windows[i], dtype=np.float32)
            out = np.empty(nf, dtype=np.float32)
            r = fn(w.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), win_len,
                   out.ctypes.data_as(ctypes.POINTER(ctypes.c_float)))
            if r != 0:
                return {"ok": False, "error": f"window {i}: feat_extract 返回 {r}"}
            got[i] = out

        tol = np.maximum(1e-3 * np.abs(expected), 5e-6)
        diff = np.abs(got.astype(np.float64) - expected.astype(np.float64))
        bad = diff > tol
        return {
            "ok": bool(not bad.any()),
            "n_windows": int(n_win),
            "max_violation": float(diff.max()) if n_win else 0.0,
            "n_violations": int(bad.sum()),
            "tol_rule": "|a-b| <= max(1e-3*|b|, 5e-6)",
        }
    finally:
        # Windows: ctypes.CDLL 持锁 .so 文件，须显式 FreeLibrary 才能 rmtree
        if hasattr(lib, "_handle") and sys.platform == "win32":
            try:
                ctypes.windll.kernel32.FreeLibrary(lib._handle)
            except Exception:  # noqa: BLE001 - 释放失败不阻塞结果
                pass


def step_static_scan(workdir: Path) -> dict:
    """④ 词边界正则白名单扫描（stub/example 不参与）。"""
    hits = []
    for p in sorted(workdir.rglob("*.[ch]")):
        if "stub" in p.parts or "example" in p.parts:
            continue
        for lineno, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            for m in BANNED_RE.finditer(line):
                hits.append(f"{p.relative_to(workdir)}:{lineno}: {m.group(0)}")
    return {"ok": not hits, "pattern": BANNED_RE.pattern, "hits": hits[:20]}


def _bAlgoMl_refs(text: str) -> set[str]:
    """从 C 源码/头文件提取 `bAlgoMl...` 符号引用集。"""
    return set(re.findall(r"\bbAlgoMl\w+\b", text))


def _algo_ml_header_symbols(repo_root: Path) -> tuple[set[str], dict]:
    """从仓库 algo_ml.h 解析声明的 bAlgoMl* 符号集（含 type）。"""
    h = repo_root / "bos" / "algorithm" / "inc" / "algo_ml.h"
    text = h.read_text(encoding="utf-8")
    decls = set(re.findall(r"\bbAlgoMl\w+\b", text))
    sha1 = hashlib.sha1(text.encode("utf-8")).hexdigest()
    return decls, {"path": str(h), "sha1": sha1}


def step_symbol_map(workdir: Path, repo_root: Path | None = None) -> dict:
    """⑤ 符号映射校验（FR-10.4 / R-INT-4）：
       a. bundle .c/.h 引用的 bAlgoMl* 必须都在仓库 algo_ml.h 里有声明
       b. bundle 编译产物中不得出现 bAlgoMl* 定义符号（nm 权威；词边界正则辅证）
       c. example 测试程序单独 gcc 零警告编译
    """
    from .c_common import find_babyos_root

    root = repo_root or find_babyos_root()
    declared, hdr_info = _algo_ml_header_symbols(root)

    # (a) bundle 引用 vs 头声明
    bad_refs: list[str] = []
    refs_total: set[str] = set()
    for p in sorted(workdir.rglob("*.[ch]")):
        if "stub" in p.parts:
            continue
        rs = _bAlgoMl_refs(p.read_text(encoding="utf-8"))
        refs_total |= rs
        unknown = sorted(rs - declared)
        if unknown:
            bad_refs.append(f"{p.relative_to(workdir)}: 引用未声明 {unknown}")
    refs_valid = sorted(refs_total & declared)

    # (b) nm 主判据：逐个 bundle .c 单独 -c 后 nm --defined-only，禁止定义 bAlgoMl*
    # ——防生成器把 algo_ml 内联/复制进 bundle
    bundle_cs = [p for p in sorted(workdir.rglob("*.c"))
                 if "stub" not in p.parts]
    defined_leaks: list[str] = []
    aux_hits: list[str] = []
    nm_per_file: dict[str, list[str]] = {}
    stub = workdir / "stub"
    for c in bundle_cs:
        obj = c.with_suffix(".o")
        cmd = ["gcc", "-c", "-std=c99", "-Wall", "-Wextra", "-ffp-contract=off",
               "-I", str(stub), "-I", str(workdir),
               "-I", str(root / "bos" / "algorithm"),
               "-I", str(root / "bos" / "algorithm" / "inc"),
               "-D_ALGO_ML_ENABLE=1", str(c), "-o", str(obj)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if r.returncode != 0:
            # 编译失败不计入 leak（在 example 检查里单独报）；跳过 nm
            continue
        nm = subprocess.run(["nm", "--defined-only", str(obj)],
                            capture_output=True, text=True, timeout=30)
        defs = re.findall(r"\bbAlgoMl\w+\b", nm.stdout)
        nm_per_file[str(c.relative_to(workdir))] = defs
        for d in defs:
            if d not in defined_leaks:
                defined_leaks.append(d)
        # 辅证正则扫描同文件
        for lineno, line in enumerate(c.read_text(encoding="utf-8").splitlines(), 1):
            if _BANNED_DEF_RE.search(line):
                aux_hits.append(f"{c.relative_to(workdir)}:{lineno}: {line.strip()[:120]}")

    # (c) example 必须单独可编译（独立 gcc 调用，零警告）
    examples = [p for p in bundle_cs if "example" in p.parts]
    ex_results = []
    for e in examples:
        ex_obj = e.with_suffix(".o")
        ex_cmd = ["gcc", "-c", "-std=c99", "-Wall", "-Wextra", "-Werror",
                  "-ffp-contract=off",
                  "-I", str(stub), "-I", str(workdir),
                  "-I", str(root / "bos" / "algorithm"),
                  "-I", str(root / "bos" / "algorithm" / "inc"),
                  "-D_ALGO_ML_ENABLE=1",
                  str(e), "-o", str(ex_obj)]
        r = subprocess.run(ex_cmd, capture_output=True, text=True, timeout=60)
        ex_results.append({
            "file": str(e.relative_to(workdir)),
            "ok": r.returncode == 0,
            "stderr": r.stderr[-2000:] if r.returncode != 0 else "",
        })

    ok = (not bad_refs) and (not defined_leaks) and all(e["ok"] for e in ex_results)
    return {
        "ok": ok,
        "algo_ml_symbols": refs_valid,
        "algo_ml_header": hdr_info,
        "bad_refs": bad_refs,
        "defined_leaks_nm": sorted(defined_leaks),
        "defined_leaks_regex": aux_hits,
        "nm_per_file": nm_per_file,
        "examples": ex_results,
    }
