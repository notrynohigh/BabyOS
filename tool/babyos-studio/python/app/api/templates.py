"""模板下载 API（FR-2.8，design §6.7）：GET /api/templates/{kind} → zip 即时打包。"""
from __future__ import annotations

from urllib.parse import quote

from fastapi import APIRouter
from fastapi.responses import Response

from ..services import template_service

router = APIRouter(prefix="/api/templates", tags=["templates"])


@router.get("/{kind}")
def get_template(kind: str) -> Response:
    body, filename = template_service.render_zip(kind)
    encoded = quote(filename)
    return Response(
        content=body,
        media_type="application/zip",
        headers={
            "Content-Disposition":
                f"attachment; filename=\"template.zip\"; filename*=UTF-8''{encoded}",
        },
    )
