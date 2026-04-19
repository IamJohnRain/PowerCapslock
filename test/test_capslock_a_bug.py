#!/usr/bin/env python3
"""
测试脚本：模拟CapsLock+A按键，验证BUG是否存在
BUG描述：按住Capslock后再按下A键，会先输出A，再触发语音转文字模块
"""

import os
import sys
import time
import ctypes
from ctypes import wintypes
import subprocess
from pathlib import Path

# Windows API 常量
VK_CAPITAL = 0x14  # CapsLock
VK_A = 0x41
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_SCANCODE = 0x0008

# 结构体定义
class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(wintypes.ULONG))
    ]

class INPUT_UNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", ctypes.c_ulong * 7), ("hi", ctypes.c_ulong * 7)]

class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("union", INPUT_UNION)]

# 加载user32.dll
user32 = ctypes.windll.user32
INPUT_KEYBOARD = 1

def send_key(vk_code, key_down=True, scan_code=0):
    """发送单个按键事件"""
    extra_info = ctypes.pointer(wintypes.ULONG(0))

    # 如果没有提供扫描码，从虚拟键码获取
    if scan_code == 0 and vk_code != 0:
        scan_code = user32.MapVirtualKeyW(vk_code, 0)
        if scan_code == 0:
            scan_code_map = {VK_CAPITAL: 0x3A, VK_A: 0x1E}
            scan_code = scan_code_map.get(vk_code, 0)

    if scan_code:
        flags = 0 if key_down else KEYEVENTF_KEYUP
        flags |= KEYEVENTF_SCANCODE
        ki = KEYBDINPUT(0, scan_code, flags, 0, extra_info)
    else:
        flags = 0 if key_down else KEYEVENTF_KEYUP
        ki = KEYBDINPUT(vk_code, 0, flags, 0, extra_info)

    inp = INPUT(INPUT_KEYBOARD, INPUT_UNION(ki=ki))
    result = user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(inp))
    return result

def press_capslock_a():
    """模拟按下CapsLock然后按A键"""
    print("模拟按键序列: CapsLock按下 -> A按下 -> A释放 -> CapsLock释放")

    print("按下CapsLock...")
    send_key(VK_CAPITAL, key_down=True)
    time.sleep(0.1)

    print("按下A键...")
    send_key(VK_A, key_down=True)
    time.sleep(0.5)

    print("释放A键...")
    send_key(VK_A, key_down=False)
    time.sleep(0.1)

    print("释放CapsLock...")
    send_key(VK_CAPITAL, key_down=False)

    print("按键序列完成")

def read_latest_log():
    """读取最新的日志文件"""
    home_dir = Path.home()
    log_dir = home_dir / '.PowerCapslock' / 'logs'

    if not log_dir.exists():
        print(f"日志目录不存在: {log_dir}")
        return None

    log_files = list(log_dir.glob('*.log'))
    if not log_files:
        print("没有找到日志文件")
        return None

    latest_log = max(log_files, key=lambda f: f.stat().st_mtime)
    print(f"读取日志文件: {latest_log}")

    try:
        with open(latest_log, 'rb') as f:
            content = f.read()
        encodings = ['utf-8', 'gbk', 'latin-1']
        for encoding in encodings:
            try:
                return content.decode(encoding, errors='replace')
            except UnicodeDecodeError:
                continue
        return content.decode('utf-8', errors='replace')
    except Exception as e:
        print(f"读取日志失败: {e}")
        return None

def check_log_for_bug(log_content):
    """检查日志中是否有BUG的迹象"""
    if not log_content:
        print("没有日志内容可分析")
        return False

    lines = log_content.split('\n')

    caps_pressed = False
    a_key_check = False
    a_key_intercepted = False

    for line in lines:
        if "CapsLock pressed" in line:
            caps_pressed = True
            print(f"找到CapsLock按下日志: {line}")

        if "[hook] Check A key:" in line:
            a_key_check = True
            print(f"找到A键检查日志: {line}")

            if "capslockHeld=1" in line:
                print("  -> CapsLock状态: 已按下")
            else:
                print("  -> CapsLock状态: 未按下 (可能有问题!)")

        if "CapsLock+A 按下，开始语音输入" in line:
            a_key_intercepted = True
            print(f"找到语音输入开始日志: {line}")

    print("\n分析结果:")
    print(f"  CapsLock按下事件: {'✓' if caps_pressed else '✗'}")
    print(f"  A键检查日志: {'✓' if a_key_check else '✗'}")
    print(f"  A键被拦截: {'✓' if a_key_intercepted else '✗'}")

    return caps_pressed and a_key_check

def main():
    print("=" * 60)
    print("PowerCapslock CapsLock+A BUG 测试脚本")
    print("=" * 60)

    # 检查PowerCapslock是否在运行
    print("检查PowerCapslock进程...")
    result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq powercapslock.exe'],
                          capture_output=True, text=True)

    if 'powercapslock.exe' not in result.stdout:
        print("警告: PowerCapslock 没有运行，测试可能不准确")
        print("请先启动 powercapslock.exe")
        choice = input("是否继续? (y/n): ")
        if choice.lower() != 'y':
            return

    # 读取测试前的日志
    print("\n读取测试前的日志...")
    before_log = read_latest_log()

    # 模拟按键
    print("\n开始模拟按键...")
    press_capslock_a()

    # 等待日志写入
    print("\n等待日志刷新...")
    time.sleep(1)

    # 读取测试后的日志
    print("读取测试后的日志...")
    after_log = read_latest_log()

    if after_log:
        if before_log:
            before_lines = before_log.split('\n')
            after_lines = after_log.split('\n')
            new_lines = [line for line in after_lines if line not in before_lines]
            new_log = '\n'.join(new_lines)

            print("\n新增的日志内容:")
            print("-" * 40)
            for line in new_lines:
                if line.strip():
                    print(line)
            print("-" * 40)

            check_log_for_bug(new_log)
        else:
            print("\n完整的日志内容:")
            print("-" * 40)
            print(after_log[-2000:])
            print("-" * 40)

            check_log_for_bug(after_log)
    else:
        print("无法读取日志文件")

    print("\n测试完成!")
    print("\nBUG分析:")
    print("如果A键被正常拦截，应该看到 'CapsLock+A 按下，开始语音输入' 日志")
    print("如果A键没有被拦截，可能会看到A键传递给了系统")
    print("检查日志中 '[hook] Check A key:' 行的 capslockHeld 状态")

if __name__ == "__main__":
    main()
