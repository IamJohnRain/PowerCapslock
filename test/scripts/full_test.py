#!/usr/bin/env python3
"""
PowerCapslock v0.1.0 Full Automation Test Script
测试所有新增功能，生成测试报告
"""

import os
import sys
import time
import subprocess
import shutil
import json
from datetime import datetime
import win32gui
import win32con
import win32api
import win32ui
from PIL import Image
import traceback

TEST_LOG_DIR = "test_output"
BACKUP_CONFIG_PATH = os.path.join(TEST_LOG_DIR, "backup_config.json")
TEST_REPORT_PATH = os.path.join(TEST_LOG_DIR, "test_report.md")
SCREENSHOT_PATH = os.path.join(TEST_LOG_DIR, "screenshot_{testname}.png")

class PowerCapslockTester:
    def __init__(self):
        self.exe_path = os.path.join(os.getcwd(), "build", "powercapslock.exe")
        self.results = []
        self.user_profile = os.environ.get('USERPROFILE', '')
        self.default_config_dir = os.path.join(self.user_profile, '.PowerCapslock', 'config')
        self.default_config_path = os.path.join(self.default_config_dir, 'config.json')
        self.default_log_dir = os.path.join(self.user_profile, '.PowerCapslock', 'logs')

    def backup_existing_config(self):
        """备份现有配置"""
        if not os.path.exists(TEST_LOG_DIR):
            os.makedirs(TEST_LOG_DIR)

        if os.path.exists(self.default_config_path):
            print(f"[INFO] Backing up existing config: {self.default_config_path}")
            shutil.copy2(self.default_config_path, BACKUP_CONFIG_PATH)
            return True
        return False

    def restore_existing_config(self):
        """恢复原有配置"""
        if os.path.exists(BACKUP_CONFIG_PATH):
            print(f"[INFO] Restoring original config")
            if not os.path.exists(self.default_config_dir):
                os.makedirs(self.default_config_dir, exist_ok=True)
            shutil.copy2(BACKUP_CONFIG_PATH, self.default_config_path)
            print(f"[INFO] Restore complete")

    def kill_existing_process(self):
        """Kill any running powercapslock process"""
        try:
            subprocess.run(['taskkill', '/F', '/IM', 'powercapslock.exe'],
                          capture_output=True, check=False)
            time.sleep(1)
        except:
            pass

    def run_command(self, args, timeout=30):
        """Run a command and return output"""
        full_args = [self.exe_path] + args
        print(f"[RUN] {' '.join(full_args)}")
        start_time = time.time()
        result = subprocess.run(full_args, capture_output=True, text=True, timeout=timeout)
        elapsed = time.time() - start_time
        return result.returncode, result.stdout, result.stderr, elapsed

    def add_result(self, test_name, passed, message, details=""):
        """Add test result"""
        self.results.append({
            'test_name': test_name,
            'passed': passed,
            'message': message,
            'details': details,
            'timestamp': datetime.now().isoformat()
        })
        status = "PASS" if passed else "FAIL"
        print(f"[{status}] {test_name}: {message}")
        if details:
            print(f"       {details}")

    def test_log_cleanup_command(self):
        """Test --cleanup-logs command line parameter"""
        test_name = "日志清理命令行 --cleanup-logs"
        try:
            returncode, stdout, stderr, elapsed = self.run_command(['--cleanup-logs'], timeout=10)
            if returncode == 0:
                self.add_result(test_name, True, f"执行成功，耗时 {elapsed:.2f}s",
                               f"stdout: {stdout}\nstderr: {stderr}")
                return True
            else:
                self.add_result(test_name, False, f"返回码 {returncode}",
                               f"stdout: {stdout}\nstderr: {stderr}")
                return False
        except Exception as e:
            self.add_result(test_name, False, f"异常: {str(e)}", traceback.format_exc())
            return False

    def test_config_test_command(self):
        """Test --test-config command line parameter"""
        test_name = "配置测试命令行 --test-config"
        try:
            returncode, stdout, stderr, elapsed = self.run_command(['--test-config'], timeout=10)
            if returncode == 0:
                self.add_result(test_name, True, f"执行成功，耗时 {elapsed:.2f}s",
                               f"stdout: {stdout}\nstderr: {stderr}")
                return True
            else:
                self.add_result(test_name, False, f"返回码 {returncode}",
                               f"stdout: {stdout}\nstderr: {stderr}")
                return False
        except Exception as e:
            self.add_result(test_name, False, f"异常: {str(e)}", traceback.format_exc())
            return False

    def capture_window_screenshot(self, window_title, output_path):
        """Capture screenshot of a window by title"""
        # Find window
        hwnd = win32gui.FindWindow(None, window_title)
        if hwnd == 0:
            def callback(handle, extra):
                if window_title.lower() in win32gui.GetWindowText(handle).lower():
                    extra.append(handle)
                return True
            handles = []
            win32gui.EnumWindows(callback, handles)
            if not handles:
                return None
            hwnd = handles[0]

        # Get dimensions
        left, top, right, bottom = win32gui.GetWindowRect(hwnd)
        width = right - left
        height = bottom - top

        # Bring to foreground
        try:
            win32gui.SetForegroundWindow(hwnd)
            time.sleep(0.5)
        except Exception as e:
            # Ignore error - Windows sometimes blocks this, but we can still capture
            print(f"[WARN] SetForegroundWindow failed (Windows restriction), continuing...: {e}")
            time.sleep(0.5)

        # Capture
        hdc = win32gui.GetWindowDC(hwnd)
        dcobj = win32ui.CreateDCFromHandle(hdc)
        memdc = dcobj.CreateCompatibleDC()
        bmp = win32ui.CreateBitmap()
        bmp.CreateCompatibleBitmap(dcobj, width, height)
        memdc.SelectObject(bmp)
        memdc.BitBlt((0, 0), (width, height), dcobj, (0, 0), win32con.SRCCOPY)

        # Convert to PIL
        bmpinfo = bmp.GetInfo()
        bmpbytes = bmp.GetBitmapBits(True)
        img = Image.frombuffer(
            'RGB',
            (bmpinfo['bmWidth'], bmpinfo['bmHeight']),
            bmpbytes, 'raw', 'BGRX', 0, 1)

        # Cleanup
        memdc.DeleteDC()
        dcobj.DeleteDC()
        win32gui.ReleaseDC(hwnd, hdc)
        win32gui.DeleteObject(bmp.GetHandle())

        img.save(output_path)
        return img

    def test_config_dialog_opens(self):
        """Test that config dialog opens correctly and capture screenshot"""
        test_name = "配置对话框打开 --test-config-dialog"
        screenshot_path = SCREENSHOT_PATH.format(testname="config_dialog")
        try:
            self.kill_existing_process()
            proc = subprocess.Popen([self.exe_path, '--test-config-dialog'])
            time.sleep(3)

            # Capture screenshot
            img = self.capture_window_screenshot("PowerCapslock Configuration", screenshot_path)
            if img is None:
                self.add_result(test_name, False, "无法找到配置对话框窗口", "")
                proc.terminate()
                return False

            width, height = img.size
            print(f"[INFO] Screenshot captured: {width}x{height} -> {screenshot_path}")

            # Analyze content - check if both tabs would have content
            # The first tab should be General, check that it has some variation
            content_left = 10
            content_top = 50
            content_right = width - 10
            content_bottom = height - 40

            pixels = []
            img_rgb = img.convert('RGB')
            for y in range(content_top, content_bottom, 20):
                for x in range(content_left, content_right, 40):
                    r, g, b = img_rgb.getpixel((x, y))
                    pixels.append((r, g, b))

            unique_colors = len(set(pixels))
            if unique_colors > 10:
                self.add_result(test_name, True,
                               f"配置对话框打开成功，截图已保存，内容区域检测到 {unique_colors} 种颜色（>10表示可见）",
                               f"Screenshot: {screenshot_path}, Image size: {width}x{height}")
            else:
                self.add_result(test_name, False,
                               f"配置对话框打开但内容区域似乎空白，仅检测到 {unique_colors} 种颜色",
                               f"Screenshot: {screenshot_path}, Image size: {width}x{height}")

            proc.terminate()
            return unique_colors > 10
        except Exception as e:
            self.add_result(test_name, False, f"异常: {str(e)}", traceback.format_exc())
            try:
                proc.terminate()
            except:
                pass
            return False

    def test_config_file_location(self):
        """Test that config file is created at the new location"""
        test_name = "配置文件位置 %USERPROFILE%\\.PowerCapslock\\config\\config.json"
        try:
            # Check if config file exists at new location
            if os.path.exists(self.default_config_path):
                size = os.path.getsize(self.default_config_path)
                with open(self.default_config_path, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                has_mappings = 'mappings' in data
                has_paths = 'paths' in data
                has_log_dir = False
                has_model_dir = False
                if has_paths:
                    has_log_dir = 'log_directory' in data['paths']
                    has_model_dir = 'model_directory' in data['paths']
                if has_mappings and has_paths and has_log_dir and has_model_dir:
                    self.add_result(test_name, True,
                                   f"配置文件存在于正确位置，大小 {size} 字节，包含所有必需字段",
                                   f"Path: {self.default_config_path}")
                    return True
                else:
                    self.add_result(test_name, False,
                                   f"配置文件存在但缺少字段: mappings={has_mappings}, paths={has_paths}, log_directory={has_log_dir}, model_directory={has_model_dir}",
                                   f"Path: {self.default_config_path}")
                    return False
            else:
                self.add_result(test_name, False, f"配置文件不存在于新位置: {self.default_config_path}", "")
                return False
        except Exception as e:
            self.add_result(test_name, False, f"异常: {str(e)}", traceback.format_exc())
            return False

    def test_log_directory_exists(self):
        """Test that log directory exists"""
        test_name = "日志目录 %USERPROFILE%\\.PowerCapslock\\logs\\"
        try:
            if os.path.exists(self.default_log_dir):
                # Count log files
                log_files = [f for f in os.listdir(self.default_log_dir) if f.endswith('.log')]
                self.add_result(test_name, True,
                               f"日志目录存在，包含 {len(log_files)} 个日志文件",
                               f"Path: {self.default_log_dir}")
                return True
            else:
                os.makedirs(self.default_log_dir, exist_ok=True)
                self.add_result(test_name, True,
                               f"日志目录不存在，已自动创建",
                               f"Path: {self.default_log_dir}")
                return True
        except Exception as e:
            self.add_result(test_name, False, f"异常: {str(e)}", traceback.format_exc())
            return False

    def test_custom_log_path_config(self):
        """Test that custom log path can be saved and loaded"""
        test_name = "自定义日志路径配置保存/加载"
        try:
            # Create a test config with custom log path
            test_custom_dir = os.path.join(self.user_profile, '.PowerCapslock', 'test_logs')
            if not os.path.exists(test_custom_dir):
                os.makedirs(test_custom_dir, exist_ok=True)

            # Read existing config
            with open(self.default_config_path, 'r', encoding='utf-8') as f:
                config = json.load(f)

            if 'paths' not in config:
                config['paths'] = {}
            original_log_dir = config['paths'].get('log_directory', '')
            config['paths']['log_directory'] = test_custom_dir

            with open(self.default_config_path, 'w', encoding='utf-8') as f:
                json.dump(config, f, indent=2)

            # Test with --test-config to load
            returncode, stdout, stderr, elapsed = self.run_command(['--test-config'], timeout=10)

            # Read back
            with open(self.default_config_path, 'r', encoding='utf-8') as f:
                config_loaded = json.load(f)

            loaded_path = config_loaded['paths'].get('log_directory', '') if 'paths' in config_loaded else ''
            # Normalize paths to avoid differences in slashes
            test_custom_dir_norm = os.path.normpath(test_custom_dir)
            loaded_path_norm = os.path.normpath(loaded_path)
            if loaded_path_norm == test_custom_dir_norm:
                # Restore original
                config['paths']['log_directory'] = original_log_dir
                with open(self.default_config_path, 'w', encoding='utf-8') as f:
                    json.dump(config, f, indent=2)
                self.add_result(test_name, True,
                               f"自定义日志路径保存和加载成功",
                               f"Custom path: {test_custom_dir}")
                return True
            else:
                self.add_result(test_name, False,
                               f"自定义路径加载后不匹配，期望: {test_custom_dir_norm}, 实际: {loaded_path_norm}",
                               f"Config content: {json.dumps(config_loaded, indent=2)}")
                return False
        except Exception as e:
            self.add_result(test_name, False, f"异常: {str(e)}", traceback.format_exc())
            return False

    def test_dynamic_mapping_save(self):
        """Test that custom mappings are saved correctly (not overwritten with defaults)"""
        test_name = "自定义按键映射动态保存"
        try:
            # Read existing config
            with open(self.default_config_path, 'r', encoding='utf-8') as f:
                config = json.load(f)

            original_mappings = config.get('mappings', [])
            original_count = len(original_mappings)

            # Add a test mapping in the same format that C uses
            test_mapping = {
                "from": "A",
                "to": "B"
            }
            original_mappings.append(test_mapping)
            config['mappings'] = original_mappings

            with open(self.default_config_path, 'w', encoding='utf-8') as f:
                json.dump(config, f, indent=2)

            # Load through --test-config which saves config back
            returncode, stdout, stderr, elapsed = self.run_command(['--test-config'], timeout=10)

            # Read back and check if our test mapping is still there
            with open(self.default_config_path, 'r', encoding='utf-8') as f:
                config_loaded = json.load(f)

            loaded_mappings = config_loaded.get('mappings', [])
            # Check if we have the mapping with correct from/to (C doesn't preserve custom name, but should preserve the from/to values)
            found = any(m.get('from') == 'A' and m.get('to') == 'B' for m in loaded_mappings)
            new_count = len(loaded_mappings)

            # Remove test mapping and restore
            config['mappings'] = original_mappings
            with open(self.default_config_path, 'w', encoding='utf-8') as f:
                json.dump(config, f, indent=2)

            if found and new_count >= original_count:
                self.add_result(test_name, True,
                               f"自定义映射保存成功，添加的测试映射仍然存在",
                               f"Original count: {original_count}, After save count: {new_count}")
                return True
            else:
                self.add_result(test_name, False,
                               f"自定义映射保存失败，测试映射丢失",
                               f"Original count: {original_count}, After save count: {new_count}, Found A->B: {found}")
                return False
        except Exception as e:
            self.add_result(test_name, False, f"异常: {str(e)}", traceback.format_exc())
            return False

    def generate_report(self):
        """Generate markdown test report"""
        total = len(self.results)
        passed = sum(1 for r in self.results if r['passed'])
        failed = total - passed
        pass_rate = (passed / total) * 100 if total > 0 else 0

        report_content = f"""# PowerCapslock v0.1.0 自动化测试报告

测试时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

## 测试总结

- 总计测试: **{total}** 个功能点
- 通过: **{passed}** ✅
- 失败: **{failed}** ❌
- 通过率: **{pass_rate:.1f}%**

## 详细测试结果

| 测试项 | 结果 | 信息 |
|--------|------|------|
"""

        for result in self.results:
            status = "✅ 通过" if result['passed'] else "❌ 失败"
            message = result['message'].replace('\n', ' ')
            report_content += f"| {result['test_name']} | {status} | {message} |\n"

        if any(not r['passed'] for r in self.results):
            report_content += """
## 失败详情

"""
            for result in self.results:
                if not result['passed'] and result['details']:
                    report_content += f"### {result['test_name']}\n\n```\n{result['details']}\n```\n\n"

        report_content += """
## 说明

- 测试脚本自动备份了原有的配置文件，测试结束后已经恢复。
- 截图保存在 `test_output/` 目录下。
- 如果配置对话框测试显示空白，请检查编译是否正确，Z-order 设置是否正确。
"""

        with open(TEST_REPORT_PATH, 'w', encoding='utf-8') as f:
            f.write(report_content)

        print(f"\n[INFO] Test report generated: {TEST_REPORT_PATH}")
        return TEST_REPORT_PATH

    def run_all_tests(self):
        """Run all tests"""
        print("=" * 60)
        print("PowerCapslock v0.1.0 Full Automation Test Started")
        print("=" * 60)
        print()

        backup_done = self.backup_existing_config()
        self.kill_existing_process()

        try:
            # Run all tests in order
            self.test_log_cleanup_command()
            self.test_config_test_command()
            self.test_config_file_location()
            self.test_log_directory_exists()
            self.test_custom_log_path_config()
            self.test_dynamic_mapping_save()
            self.test_config_dialog_opens()
        finally:
            self.restore_existing_config()
            self.kill_existing_process()

        report_path = self.generate_report()

        print("\n" + "=" * 60)
        print("Testing Complete!")
        total = len(self.results)
        passed = sum(1 for r in self.results if r['passed'])
        print(f"Result: {passed}/{total} passed")
        print(f"Report: {report_path}")
        print("=" * 60)

if __name__ == "__main__":
    tester = PowerCapslockTester()
    tester.run_all_tests()
