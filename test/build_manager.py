#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
测试文件编译管理脚本
使用方法：输入要编译的文件id，支持单文件(1)、多文件(1 3)、范围(4-6)
"""

import os
import subprocess
import re
import glob

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(SCRIPT_DIR, "..", "src")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "compiled")


def ensure_output_dir():
    """确保输出目录存在"""
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)


def scan_test_files():
    """扫描test目录下的cpp文件，按序号排序"""
    pattern = os.path.join(SCRIPT_DIR, "*.cpp")
    files = glob.glob(pattern)

    file_list = []
    for f in files:
        basename = os.path.basename(f)
        # 匹配 "数字_xxx.cpp" 格式
        match = re.match(r'^(\d+)_(.+)\.cpp$', basename)
        if match:
            file_id = int(match.group(1))
            file_list.append({
                'id': file_id,
                'name': basename,
                'path': f
            })

    file_list.sort(key=lambda x: x['id'])
    return file_list


def parse_selection(input_str):
    """解析用户输入，如 '1 3 4-6' -> [1, 3, 4, 5, 6]"""
    selected = set()
    parts = input_str.strip().split()

    for part in parts:
        part = part.strip()
        if not part:
            continue

        if '-' in part:
            # 范围选择，如 "4-6"
            range_match = re.match(r'^(\d+)-(\d+)$', part)
            if range_match:
                start = int(range_match.group(1))
                end = int(range_match.group(2))
                if start > end:
                    print(f"警告: 范围 {part} 无效，跳过")
                    continue
                selected.update(range(start, end + 1))
            else:
                print(f"警告: 无效范围格式 '{part}'，跳过")
        elif part.isdigit():
            # 单个数字
            selected.add(int(part))
        else:
            print(f"警告: 无法识别的输入 '{part}'，跳过")

    return sorted(selected)


def compile_file(src_path, exe_name=None):
    """编译单个cpp文件"""
    if exe_name is None:
        exe_name = os.path.splitext(os.path.basename(src_path))[0] + ".exe"
    exe_path = os.path.join(OUTPUT_DIR, exe_name)

    # 源文件列表（测试文件 + audioPlayer.cpp）
    src_files = [src_path, os.path.join(SRC_DIR, "audioPlayer.cpp")]

    # 构建包含目录
    include_dirs = [
        SCRIPT_DIR,
        SRC_DIR
    ]
    include_args = []
    for inc_dir in include_dirs:
        include_args.extend(["-I", inc_dir])

    # 链接库 (Windows ACM)
    link_args = ["-lmsacm32", "-lwinmm", "-lcomdlg32"]

    cmd = ["g++", "-std=c++17", "-o", exe_path] + src_files + include_args + link_args

    print(f"\n编译: {os.path.basename(src_path)}")
    print(f"命令: {' '.join(cmd)}")

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')
        if result.returncode == 0:
            print(f"成功: {exe_name}")
            return True
        else:
            print(f"编译失败:")
            print(result.stderr)
            return False
    except Exception as e:
        print(f"编译出错: {e}")
        return False


def main():
    files = scan_test_files()

    if not files:
        input("未找到测试文件，按回车键继续")
        return

    ensure_output_dir()

    print("=" * 50)
    print("测试文件列表:")
    print("=" * 50)
    for f in files:
        print(f"  [{f['id']}] {f['name']}")
    print("=" * 50)
    print("  [all] 编译全部文件")
    print("=" * 50)
    print()

    try:
        selection = input("输入想要编译的文件: ").strip()
        if not selection:
            input("未输入任何选择，按回车键继续")
            return

        # 处理 all 选项
        if selection.lower() == "all":
            selected_ids = [f['id'] for f in files]
        else:
            selected_ids = parse_selection(selection)

        if not selected_ids:
            input("没有有效的选择，按回车键继续")
            return

        # 构建 id -> file 映射
        id_to_file = {f['id']: f for f in files}

        success_count = 0
        fail_count = 0

        for file_id in selected_ids:
            if file_id in id_to_file:
                f = id_to_file[file_id]
                if compile_file(f['path']):
                    success_count += 1
                else:
                    fail_count += 1
            else:
                print(f"警告: 文件 id={file_id} 不存在")

        print("\n" + "=" * 50)
        print(f"编译完成: 成功 {success_count}, 失败 {fail_count}")
        print("=" * 50)
        
        input("按回车键结束本轮编译")

    except KeyboardInterrupt:
        print("\n已取消")


if __name__ == "__main__":
    while True:
        os.system('cls')
        main()
