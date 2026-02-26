import os

def modify_b_driver_h(drv_upper, drv_lower):
    """
    修改inc/b_driver.h：在B_DRIVER_NUMBER上一行插入B_DRIVER_XXXX,
    :param drv_upper: 驱动名大写（如UART）
    :param drv_lower: 驱动名小写（如uart）
    """
    file_path = "inc/b_driver.h"
    # 检查文件是否存在
    if not os.path.exists(file_path):
        print(f"警告：{file_path} 文件不存在，跳过该文件修改")
        return
    
    # 要插入的内容
    insert_line = f"B_DRIVER_{drv_upper},"
    # 目标匹配行
    target_line = "B_DRIVER_NUMBER"
    
    try:
        # 读取文件所有行
        with open(file_path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        
        # 检查是否已存在该宏，避免重复插入
        for line in lines:
            if insert_line.strip() in line.strip():
                print(f"{file_path} 中已存在 {insert_line}，无需重复插入")
                return
        
        # 找到目标行并插入
        insert_index = -1
        for idx, line in enumerate(lines):
            if target_line in line:
                insert_index = idx  # 目标行的索引，插入到它的上一行
                break
        
        if insert_index == -1:
            print(f"警告：在 {file_path} 中未找到 {target_line}，跳过插入")
            return
        
        # 在目标行上一行插入
        lines.insert(insert_index, f"    {insert_line}\n")  # 加缩进保持格式统一
        
        # 写回文件
        with open(file_path, "w", encoding="utf-8") as f:
            f.writelines(lines)
        
        print(f"已在 {file_path} 中插入：{insert_line}")
    
    except Exception as e:
        print(f"修改 {file_path} 失败：{e}")

def modify_b_os_h(drv_upper, drv_lower):
    """
    修改../b_os.h：在#include "drivers/inc/b_drv_24cxx.h"上一行插入头文件包含
    :param drv_upper: 驱动名大写（如UART）
    :param drv_lower: 驱动名小写（如uart）
    """
    file_path = "../b_os.h"
    # 检查文件是否存在
    if not os.path.exists(file_path):
        print(f"警告：{file_path} 文件不存在，跳过该文件修改")
        return
    
    # 要插入的内容
    insert_line = f'#include "drivers/inc/b_drv_{drv_lower}.h"'
    # 目标匹配行
    target_line = '#include "drivers/inc/b_drv_24cxx.h"'
    
    try:
        # 读取文件所有行
        with open(file_path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        
        # 检查是否已存在该包含语句，避免重复插入
        for line in lines:
            if insert_line.strip() in line.strip():
                print(f"{file_path} 中已存在 {insert_line}，无需重复插入")
                return
        
        # 找到目标行并插入
        insert_index = -1
        for idx, line in enumerate(lines):
            if target_line in line:
                insert_index = idx  # 目标行的索引，插入到它的上一行
                break
        
        if insert_index == -1:
            print(f"警告：在 {file_path} 中未找到 {target_line}，跳过插入")
            return
        
        # 在目标行上一行插入
        lines.insert(insert_index, f"{insert_line}\n")
        
        # 写回文件
        with open(file_path, "w", encoding="utf-8") as f:
            f.writelines(lines)
        
        print(f"已在 {file_path} 中插入：{insert_line}")
    
    except Exception as e:
        print(f"修改 {file_path} 失败：{e}")

def modify_b_hal_if_h(drv_upper, drv_lower):
    """
    修改../../_config/b_hal_if.h：从文件最后往前找#endif，在其上方插入#define HAL_XXXX_IF {}
    :param drv_upper: 驱动名大写（如UART）
    :param drv_lower: 驱动名小写（如uart）
    """
    file_path = "../../_config/b_hal_if.h"
    # 检查文件是否存在（先检查目录，再检查文件）
    config_dir = os.path.dirname(file_path)
    if not os.path.exists(config_dir):
        print(f"警告：{config_dir} 目录不存在，跳过该文件修改")
        return
    if not os.path.exists(file_path):
        print(f"警告：{file_path} 文件不存在，跳过该文件修改")
        return
    
    # 要插入的内容
    insert_line = f"#define HAL_{drv_upper}_IF {{}}"
    # 目标匹配关键词（忽略前后空格）
    target_key = "#endif"
    
    try:
        # 读取文件所有行（保留换行符）
        with open(file_path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        
        # 检查是否已存在该宏定义，避免重复插入
        for line in lines:
            if insert_line.strip() in line.strip():
                print(f"{file_path} 中已存在 {insert_line}，无需重复插入")
                return
        
        # 从文件最后往前遍历，找到第一个包含#endif的行
        insert_index = -1
        for idx in range(len(lines)-1, -1, -1):
            line_content = lines[idx].strip()
            if target_key in line_content:
                insert_index = idx  # 找到#endif行的索引，插入到它的上一行
                break
        
        if insert_index == -1:
            print(f"警告：在 {file_path} 中未找到 {target_key}，跳过插入")
            return
        
        # 在#endif行的上一行插入（保持和周边代码一致的缩进，这里默认无缩进，可根据实际调整）
        lines.insert(insert_index, f"{insert_line}\n")
        
        # 写回文件
        with open(file_path, "w", encoding="utf-8") as f:
            f.writelines(lines)
        
        print(f"已在 {file_path} 中插入：{insert_line}")
    
    except Exception as e:
        print(f"修改 {file_path} 失败：{e}")

def create_driver_files():
    # 1. 获取用户输入的驱动名并处理格式
    driver_name = input("请输入驱动名（例如ds18b20）：").strip()
    if not driver_name:
        print("错误：驱动名不能为空！")
        return
    
    # 统一处理为小写（文件名）和大写（宏/结构体）
    drv_lower = driver_name.lower()
    drv_upper = driver_name.upper()
    
    # 2. 定义C文件模板（替换所有xxxx/XXXX占位符）
    c_template = '''/**
 *!
 * \\file        b_drv_{drv_lower}.c
 * \\version     v0.0.1
 * \\date        2023/03/25
 * \\author      babyos
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 babyos
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
#include "drivers/inc/b_drv_{drv_lower}.h"

/**
 * \\addtogroup B_DRIVER
 * \\{{
 */

/**
 * \\addtogroup {drv_upper}
 * \\{{
 */

/**
 * \\defgroup {drv_upper}_Private_TypesDefinitions
 * \\{{
 */

/**
 * \\}}
 */

/**
 * \\defgroup {drv_upper}_Private_Defines
 * \\{{
 */
#define DRIVER_NAME {drv_upper}
/**
 * \\}}
 */

/**
 * \\defgroup {drv_upper}_Private_Macros
 * \\{{
 */

/**
 * \\}}
 */

/**
 * \\defgroup {drv_upper}_Private_Variables
 * \\{{
 */
bDRIVER_HALIF_TABLE(b{drv_upper}_HalIf_t, DRIVER_NAME);

static b{drv_upper}Private_t b{drv_upper}RunInfo[bDRIVER_HALIF_NUM(b{drv_upper}_HalIf_t, DRIVER_NAME)];

/**
 * \\}}
 */

/**
 * \\defgroup {drv_upper}_Private_FunctionPrototypes
 * \\{{
 */

/**
 * \\}}
 */

/**
 * \\defgroup {drv_upper}_Private_Functions
 * \\{{
 */

static int _b{drv_upper}Write(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{{
    bDRIVER_GET_HALIF(_if, b{drv_upper}_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, b{drv_upper}Private_t, pdrv);
    return len;
}}

static int _b{drv_upper}Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{{
    bDRIVER_GET_HALIF(_if, b{drv_upper}_HalIf_t, pdrv);
    bDRIVER_GET_PRIVATE(_priv, b{drv_upper}Private_t, pdrv);
    return len;
}}

static int _b{drv_upper}Ctl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{{
    bDRIVER_GET_PRIVATE(_priv, b{drv_upper}Private_t, pdrv);
    return 0;
}}

static int _b{drv_upper}Open(bDriverInterface_t *pdrv)
{{
    bDRIVER_GET_HALIF(_if, b{drv_upper}_HalIf_t, pdrv);
    return 0;
}}

static int _b{drv_upper}Close(bDriverInterface_t *pdrv)
{{
    bDRIVER_GET_HALIF(_if, b{drv_upper}_HalIf_t, pdrv);
    return 0;
}}

/**
 * \\}}
 */

/**
 * \\addtogroup {drv_upper}_Exported_Functions
 * \\{{
 */
int b{drv_upper}_Init(bDriverInterface_t *pdrv)
{{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, b{drv_upper}_Init);
    pdrv->read  = _b{drv_upper}Read;
    pdrv->write = _b{drv_upper}Write;
    pdrv->ctl   = _b{drv_upper}Ctl;
    pdrv->open  = _b{drv_upper}Open;
    pdrv->close = _b{drv_upper}Close;
    pdrv->_private._p = &b{drv_upper}RunInfo[pdrv->drv_no];
    return 0;
}}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_{drv_upper}, b{drv_upper}_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif
/**
 * \\}}
 */

/**
 * \\}}
 */

/**
 * \\}}
 */

/************************ Copyright (c) 2023 babyos*****END OF FILE****/
'''

    # 3. 定义H文件模板（替换所有xxxx/XXXX占位符）
    h_template = '''/**
 *!
 * \\file        b_drv_{drv_lower}.h
 * \\version     v0.0.1
 * \\date        2023/03/25
 * \\author      babyos
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 babyos
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
#ifndef __B_DRV_{drv_upper}_H__
#define __B_DRV_{drv_upper}_H__

#ifdef __cplusplus
extern "C" {{
#endif

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_driver.h"
/**
 * \\addtogroup B_DRIVER
 * \\{{
 */

/**
 * \\addtogroup {drv_upper}
 * \\{{
 */

/**
 * \\defgroup {drv_upper}_Exported_TypesDefinitions
 * \\{{
 */

typedef bHalI2CIf_t b{drv_upper}_HalIf_t;

typedef struct
{{
    char reserved;
}} b{drv_upper}Private_t;

/**
 * \\}}
 */

/**
 * \\}}
 */

/**
 * \\}}
 */

#ifdef __cplusplus
}}
#endif

#endif

/************************ Copyright (c) 2023 babyos *****END OF FILE****/
'''

    try:
        # 4. 创建inc目录（如果不存在）
        if not os.path.exists("inc"):
            os.makedirs("inc")
            print(f"已创建inc目录")
        
        # 5. 生成C文件
        c_filename = f"b_drv_{drv_lower}.c"
        with open(c_filename, "w", encoding="utf-8") as f:
            f.write(c_template.format(drv_lower=drv_lower, drv_upper=drv_upper))
        print(f"已创建文件：{c_filename}")
        
        # 6. 生成H文件
        h_filename = f"inc/b_drv_{drv_lower}.h"
        with open(h_filename, "w", encoding="utf-8") as f:
            f.write(h_template.format(drv_lower=drv_lower, drv_upper=drv_upper))
        print(f"已创建文件：{h_filename}")
        
        # ========== 依次修改指定文件 ==========
        modify_b_driver_h(drv_upper, drv_lower)
        modify_b_os_h(drv_upper, drv_lower)
        modify_b_hal_if_h(drv_upper, drv_lower)  # 新增调用
        
    except Exception as e:
        print(f"创建/修改文件失败：{e}")

if __name__ == "__main__":
    create_driver_files()