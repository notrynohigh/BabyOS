import os
import shutil
import re


def create_folder(folder_name):
    folder_exist = 0
    # 获取当前目录
    current_dir = os.getcwd()
    # 拼接文件夹路径
    folder_path = os.path.join(current_dir, folder_name)
    # 判断文件夹是否存在
    if not os.path.exists(folder_path):
        # 创建文件夹
        os.makedirs(folder_path)
        folder_exist = 1
        print(f"文件夹 '{folder_name}' 创建成功！")
    else:
        print(f"文件夹 '{folder_name}' 已经存在。")
    return folder_path, folder_exist



vendor = input("please enter vendor name:")
vendor_dir, vendor_exist = create_folder(vendor)
os.chdir(vendor_dir)

mcu = input("please enter mcu name:")
mcu_dir, mcu_exist = create_folder(mcu)
os.chdir(mcu_dir)

if mcu_exist == 1:
    hal_dir = "../../../hal/"
    current_dir = "."
    # 获取目录hal_dir中以"b_hal_"开头的所有c文件
    c_files = [file for file in os.listdir(hal_dir) if file.startswith("b_hal_") and file.endswith(".c")]
    # 在当前目录创建对应的以"b_mcu_"开头的c文件
    for c_file in c_files:
        new_file = "mcu_" + mcu + "_" + c_file[6:]
        target_filepath = os.path.join(current_dir, new_file)
        print(target_filepath)
        with open(target_filepath, 'w') as target_file:
            include_line = "#include \"b_config.h\""
            with open("../../../hal/b_hal_" + c_file[6:], "r", encoding='utf-8') as file:
                lines = file.readlines()
                filtered_lines = []
                check_flag = 0
                for cpline in lines:
                    if cpline.startswith("__WEAKDEF") and check_flag == 0:
                        cpline = cpline.replace('__WEAKDEF', '')
                        cpline = re.sub(r'^\s+', '', cpline)
                        filtered_lines.append(cpline)
                        if "}" in cpline:
                            check_flag = 0
                        else:
                            check_flag = 1
                    elif check_flag == 1:
                        filtered_lines.append(cpline)
                        if "}" in cpline:
                            filtered_lines.append("\n")
                            check_flag = 0
                file.close()
            # 写入"#include xxxxxx"行
            target_file.write("#include \"b_config.h\"" + "\n")
            target_file.write("#include \"hal/inc/b_hal_" + c_file[6:-1] + 'h\"' + "\n\n")
            # 写入包含"bMcu"的行
            target_file.writelines(filtered_lines)
            target_file.close()











