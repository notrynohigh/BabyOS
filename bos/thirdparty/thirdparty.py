#!/usr/bin/env python
# -*- coding: UTF-8 -*-

import os, sys
import shutil
import re

def find_files(dirs, des):
    source_file = []
    source_file_des = []
    for d in dirs:
        files = os.listdir(d)
        for file_name in files:
            # 检查文件扩展名是否为.c或.h
            if file_name.endswith('.c') or file_name.endswith('.h') or file_name.endswith('.inc'):
                # 构建源文件和目标文件的完整路径
                source_file.append(os.path.join(d, file_name))
                source_file_des.append(os.path.join(des, file_name))
    return source_file,source_file_des

def clear_directory(directory):
    # 获取目录中的所有文件和子目录
    for filename in os.listdir(directory):
        file_path = os.path.join(directory, filename)
        # 判断是否是文件
        if os.path.isfile(file_path):
            # 删除文件
            os.remove(file_path)
        # 如果是子目录，则递归清空子目录中的文件
        elif os.path.isdir(file_path):
            clear_directory(file_path)

#将文件里面的相对路径，去掉相对路径部分字符串
def process_includes(file_path):
    with open(file_path, 'r', encoding="utf-8") as f:
        content = f.read()
    # 使用正则表达式匹配所有#include语句，并去掉路径部分
    pattern = r'#include\s+"(\.\./)*(\S*/)*(.+?)"'
    content = re.sub(pattern, r'#include "\3"', content)
    # 使用正则表达式匹配所有#include语句，并去掉路径部分
    pattern = r'#include\s+<(\.\./)*(\S*/)*(.+?)>'
    content = re.sub(pattern, r'#include "\3"', content)
    with open(file_path, 'w', encoding="utf-8") as f:
        f.write(content)

def copy_files_R(src_dir, des_dir):
    try:
        shutil.copytree(src_dir, des_dir)
        print("目录复制成功！")
    except FileExistsError:
        print("目标目录已存在！")
    except:
        print("目录复制失败！")

#在文件里相对路径前增加一个路径
def replace_includes(directory, replacement):
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(".c") or file.endswith(".h"):
                file_path = os.path.join(root, file)
                with open(file_path, 'r') as f:
                    content = f.read()
                content = content.replace('#include "lwip/', '#include "{}lwip/'.format(replacement))
                content = content.replace('#include "netif/', '#include "{}netif/'.format(replacement))
                with open(file_path, 'w') as f:
                    f.write(content)

# 拷贝文件到目录
def copy_file_to_directory(file, des_dir):
    shutil.copy(file, des_dir)

#---------------------------------------------------------------------
#---------------------------------------------------------------------
#---------------------------------------------------------------------
# arm-2d
def cp_arm_2d_file(bos_dir):
    arm_2d_dir = bos_dir + "/thirdparty/arm-2d/"
    tmp_dir = arm_2d_dir + "bos_arm-2d/"
    if not os.path.exists(tmp_dir):
        os.makedirs(tmp_dir)
    else:
        print("bos_arm-2d exist !")
        return
    arm_2d_file_dir = [arm_2d_dir + "Helper/Source/", arm_2d_dir + "Helper/Include/",
                       arm_2d_dir + "Library/Source/", arm_2d_dir + "Library/Include/", 
                       arm_2d_dir + "examples/[template][babyos]/",
                       arm_2d_dir + "examples/common/controls/",
                       arm_2d_dir + "examples/common/asset/"]
    arm_2d_files,tmp_dir_files = find_files(arm_2d_file_dir, tmp_dir)
    for i in range(len(arm_2d_files)):
        shutil.copy2(arm_2d_files[i], tmp_dir_files[i])
        process_includes(tmp_dir_files[i])

# lwip
def cp_lwip_file(bos_dir):
    lwip_dir = bos_dir + "/thirdparty/lwip/"
    tmp_dir = lwip_dir + "bos_lwip/"
    if not os.path.exists(tmp_dir):
        os.makedirs(tmp_dir)
    else:
        print("bos_lwip exist !")
        return
    
    lwip_c_file_dir = [lwip_dir + "src/core/", lwip_dir + "src/core/ipv4/", lwip_dir + "test/bos/arch/"]
    lwip_c_files,tmp_dir_files = find_files(lwip_c_file_dir, tmp_dir)
    for i in range(len(lwip_c_files)):
        shutil.copy2(lwip_c_files[i], tmp_dir_files[i])
    copy_files_R(lwip_dir + "src/include/", tmp_dir + "include/")
    copy_files_R(lwip_dir + "test/bos/arch/", tmp_dir + "include/lwip/arch/")
    copy_file_to_directory(lwip_dir + "test/bos/lwipopts.h", tmp_dir + "include/lwip/")
    copy_file_to_directory(lwip_dir + "test/bos/netif/ethernet.c", tmp_dir)
    replace_includes(tmp_dir, "thirdparty/lwip/bos_lwip/include/")

#---------------------------------------------------------------------
#---------------------------------------------------------------------
def copy_specific_files(source_dir, destination_dir, extensions, del_include_path):
    for root, dirs, files in os.walk(source_dir):
        for file in files:
            if file.endswith(tuple(extensions)):
                source_file = os.path.join(root, file)
                destination_file = os.path.join(destination_dir, file)
                shutil.copy2(source_file, destination_file)
                if del_include_path:
                    process_includes(destination_file)

def replace_line_with_pattern(file_path, pattern_to_replace, replacement_line):
    with open(file_path, 'r', encoding="utf-8") as file:
        lines = file.readlines()
    with open(file_path, 'w', encoding="utf-8") as file:
        for line in lines:
            if pattern_to_replace in line:
                file.write(replacement_line + '\n')
            else:
                file.write(line)

def replace_line_with_pattern_plus(file_path, pattern_to_replace, replacement_line):
    # 读取文件内容
    with open(file_path, 'r', encoding="utf-8") as file:
        file_content = file.read()
    # 使用正则表达式进行替换
    file_content = re.sub(pattern_to_replace, replacement_line, file_content)
    # 写入替换后的内容
    with open(file_path, 'w', encoding="utf-8") as file:
        file.write(file_content)
                
# tinyusb
def cp_tinyusb_file(bos_dir):
    tinyusb_dir = bos_dir + "/thirdparty/tinyusb/"
    tmp_dir = tinyusb_dir + "bos_tinyusb/"
    if not os.path.exists(tmp_dir):
        os.makedirs(tmp_dir)
    else:
        print("bos_tinyusb exist !")
        return
    extensions = (".c", ".h")
    copy_specific_files(tinyusb_dir + "tinyusb/src/", tmp_dir, extensions, True)
    replace_line_with_pattern(tmp_dir + 'tusb_option.h','#define _TUSB_OPTION_H_', '#define _TUSB_OPTION_H_\r\n#include "b_config.h"')
    pattern = r'#\s*define\s+tu_printf\s+printf'
    replace_line_with_pattern_plus(tmp_dir + 'tusb_debug.h', pattern, '#include "utils/inc/b_util_log.h"\r\n  #define tu_printf  b_log')

#---------------------------------------------------------------------
def cp_thirdparty_file(bos_dir):
    cp_arm_2d_file(bos_dir)
    cp_lwip_file(bos_dir)
    cp_tinyusb_file(bos_dir)
