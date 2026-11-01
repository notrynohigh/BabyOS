"""b_os.h stub（自检编译用）与 BabyOS 仓库根定位（FR-10，design §7.5）。

v1.6 起公共 ML 原语不再随 bundle 下发——由 BabyOS 预置 algo_ml 模块提供
（bos/algorithm/algo_ml.h，R-INT-4 清单机制）；本文件仅保留：
- STUB_B_OS_H：导出自检的 host 编译最小 b_os.h（含 PT 宏最小集，example 编译用）
- find_babyos_root：向上定位含 bos/algorithm/algo_ml.h 的仓库根（自检锚点）
"""
from __future__ import annotations

from pathlib import Path

STUB_B_OS_H = """/* b_os.h stub —— 仅用于导出自检的 host 编译（FR-8.5 ①），勿编入固件 */
#ifndef _B_OS_STUB_H_
#define _B_OS_STUB_H_
#include <stdio.h>
#include <stdlib.h>

#define bMalloc(size)      malloc(size)
#define bFree(ptr)         free(ptr)
#define b_log_e(...)       fprintf(stderr, __VA_ARGS__)
#define b_log_i(...)       fprintf(stderr, __VA_ARGS__)
#define b_log_w(...)       fprintf(stderr, __VA_ARGS__)

/* PT 宏最小集（bos/thirdparty/pt 的 host 替身）——example 测试程序编译用。
   语义与真实 PT 一致（switch-case 状态机），仅调度由用户 bExec() 驱动。 */
struct pt
{
    unsigned short lc;
};
#define PT_WAITING 0
#define PT_YIELDED 1
#define PT_EXITED  2
#define PT_ENDED   3
#define PT_INIT(pt)                    do { (pt)->lc = 0; } while (0)
/* PT_THREAD(name)(args...) 展开为静态函数定义；`name` 紧跟 `(` 用于函数名位置 */
#define PT_THREAD(name)                int name
#define PT_BEGIN(pt)                   { switch ((pt)->lc) { case 0:
#define PT_END(pt)                     } (pt)->lc = 0; return PT_ENDED; }
#define PT_DELAY_MS(pt, ms)            \\
    do { (pt)->lc = (unsigned short)__LINE__; case __LINE__: ; } while (0)
#define BOS_REG_POLLING_FUNC(fn)       /* host stub: 注册动作无操作 */
#endif
"""

_REPO_MARK = "bos/algorithm/inc/algo_ml.h"


def find_babyos_root(start: Path | None = None) -> Path:
    """向上找含 bos/algorithm/algo_ml.h 的仓库根（自检锚点）。

    默认从本文件位置出发（backend 位于 BabyOS 仓库内的 tool/automl/ 下）。
    找不到 → AppError 422 ALGO_ML_NOT_FOUND（导出前置条件，FR-10）。
    """
    from ...deps import AppError

    p = (start or Path(__file__)).resolve()
    for cand in (p, *p.parents):
        if (cand / _REPO_MARK).is_file():
            return cand
    raise AppError(422, "ALGO_ML_NOT_FOUND",
                   f"未找到 BabyOS 仓库根（含 {_REPO_MARK}）；"
                   "导出需基于 BabyOS 仓库内的 automl 后端运行（FR-10）")
