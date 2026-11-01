# 模块 22：Menu 菜单

**文件**: `bos/modules/b_mod_menu.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Menu 模块实现嵌入式双向链表菜单导航。

### 1.2 核心 API

```c
// 添加节点
int bMenuAddSibling(uint32_t ref_id, uint32_t id, pCreateUI f);
int bMenuAddChild(uint32_t ref_id, uint32_t id, pCreateUI f);

// 菜单操作
void bMenuAction(uint8_t cmd);   // MENU_UP/MENU_DOWN/MENU_BACK/MENU_ENTER

// 跳转/显示控制
void bMenuJump(uint32_t id);
int bMenuSetVisible(uint32_t id, uint8_t s);

// 获取当前 ID
uint32_t bMenuCurrentID(void);

// 重置
void bMenuReset(void);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bMenuAddSibling` | 添加根节点和兄弟节点 | 返回正确 ItemIndex |
| `test_bMenuAddChild` | 添加子节点 | 子节点正确添加 |
| `test_bMenuAction` | UP/DOWN 导航 | 循环遍历正确 |
| `test_bMenuJump` | 跳转到指定 ID | 当前 ID 正确 |
| `test_bMenuSetVisible` | 设置节点可见性 | 导航行为改变 |
| `test_bMenuCurrentID` | 获取当前 ID | 正确反映状态 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_menu.c`
- `bos/modules/inc/b_mod_menu.h`
- 无硬件依赖

---

## 四、备注

`bMenuReset` 在每次测试前调用以确保干净状态。