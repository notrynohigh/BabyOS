"""FastAPI 应用入口：路由注册、CORS、启动对账（design.md §3）。"""
import logging
import shutil
import traceback

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

from .api import (
    datasets,
    device,
    export,
    features,
    labels,
    projects,
    segments,
    templates,
    training,
)
from .config import projects_root
from .deps import AppError, atomic_write_json, read_json

__version__ = "1.0.0"
log = logging.getLogger("babyos.automl")


def create_app() -> FastAPI:
    app = FastAPI(title="BabyOS AutoML", version=__version__)
    app.add_middleware(
        CORSMiddleware,
        allow_origin_regex=r"^http://127\.0\.0\.1:\d+$|^http://localhost:\d+$",
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.exception_handler(Exception)
    async def _unhandled(_request: Request, exc: Exception):
        """兜底：未捕获异常 → 422 + 结构化 detail，避免裸 500（评审 P1-5）。"""
        log.exception("unhandled exception: %s", exc)
        return JSONResponse(
            status_code=422,
            content={"detail": {"code": "UNHANDLED", "detail": str(exc),
                               "trace": traceback.format_exc(limit=8).splitlines()[-12:]}},
        )

    @app.get("/api/version")
    async def get_version():
        return {"version": __version__}

    app.include_router(projects.router)
    app.include_router(datasets.router)
    app.include_router(labels.router)
    app.include_router(segments.router)
    app.include_router(features.router)
    app.include_router(training.router)
    app.include_router(export.router)
    app.include_router(templates.router)
    app.include_router(device.router)

    @app.on_event("startup")
    def _reconcile() -> None:
        """启动对账（P2-6）：残留 running → interrupted；孤儿 npz 清理。"""
        root = projects_root()
        if not root.is_dir():
            return
        for d in root.iterdir():
            if not d.is_dir():
                continue
            st = read_json(d / "training" / "run_state.json")
            if st and st.get("status") == "running":
                st["status"] = "interrupted"
                st["error"] = "进程重启，训练中断"
                atomic_write_json(d / "training" / "run_state.json", st)
                for sub in ("candidates", "best"):
                    shutil.rmtree(d / "training" / sub, ignore_errors=True)
                flag = d / "training" / "cancel.flag"
                if flag.exists():
                    flag.unlink()
            # files.json ↔ npz 对账
            files = read_json(d / "raw" / "files.json", default={})
            if isinstance(files, dict):
                for fid in list(files.keys()):
                    if not (d / "raw" / f"{fid}.npz").exists():
                        del files[fid]
                atomic_write_json(d / "raw" / "files.json", files)

    # 前端构建产物（M6 后存在；缺失时跳过，不阻塞后端）
    # SPA fallback：非 API、非静态文件的请求一律返回 index.html（支持前端路由刷新）
    from pathlib import Path

    fe_dist = Path(__file__).resolve().parents[2] / "frontend" / "dist"
    index_html = fe_dist / "index.html" if fe_dist.is_dir() else None

    if index_html and index_html.is_file():
        from starlette.responses import FileResponse

        @app.get("/{full_path:path}", include_in_schema=False)
        async def spa_fallback(full_path: str, request: Request):
            # 静态资源（js/css/png...）正常返回
            file = fe_dist / full_path
            if file.is_file():
                return FileResponse(file)
            # 其余一律返回 index.html（SPA 路由接管）
            return FileResponse(index_html)

    return app


app = create_app()
