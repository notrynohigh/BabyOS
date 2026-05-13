# 模块 05：Sort 排序算法

**文件**: `bos/algorithm/algo_sort.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Sort 模块实现三种基础排序算法：冒泡排序、选择排序和插入排序。纯软件实现，无任何硬件依赖。

### 1.2 核心 API

```c
// 冒泡排序
void BubbleSort(void *arr, int size, uint8_t type);

// 选择排序
void SelectionSort(void *arr, int size, uint8_t type);

// 插入排序
void InsertionSort(void *arr, int size, uint8_t type);

// type 参数：_ALGO_SORT_I8/_ALGO_SORT_U8/_ALGO_SORT_I16/_ALGO_SORT_U16/_ALGO_SORT_I32/_ALGO_SORT_U32
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bSortBubble` | int32 冒泡排序 {5,3,8,1,9,2,7,4,6,0} | {0,1,2,3,4,5,6,7,8,9} |
| `test_bSortInsertion` | int32 插入排序 {5,3,8,1,9} | {1,3,5,8,9} |
| `test_bSortSelect` | int32 选择排序 {9,1,8,2,7,3,6,4,5,0} | {0,1,2,3,4,5,6,7,8,9} |

---

## 三、测试文件

```
test/selftest/test_algo.c
```

**依赖**:
- `bos/algorithm/algo_sort.c`
- 无任何硬件依赖

---

## 四、备注

三种算法均为原地排序，O(n²) 复杂度，适合小数据集。