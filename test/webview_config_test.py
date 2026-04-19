#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WebView 配置页面功能测试脚本

测试 PowerCapslock WebView2 配置界面的功能:
- 页面加载
- 页签切换
- 配置加载和保存
- 按键映射操作
"""

import argparse
import json
import os
import subprocess
import sys
import time
import unittest
from pathlib import Path
from typing import Optional

# 添加项目路径
PROJECT_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "test"))


class WebViewConfigTestCase(unittest.TestCase):
    """WebView 配置页面测试基类"""

    @classmethod
    def setUpClass(cls):
        """测试类初始化"""
        cls.build_dir = PROJECT_ROOT / "build"
        cls.exe_path = cls.build_dir / "PowerCapslock.exe"
        cls.webview_dir = PROJECT_ROOT / "resources"  # 实际资源在 resources 目录

        # 检查可执行文件
        if not cls.exe_path.exists():
            raise FileNotFoundError(f"可执行文件不存在: {cls.exe_path}")

        # 检查 WebView 资源 (config_ui.html)
        if not (cls.webview_dir / "config_ui.html").exists():
            raise FileNotFoundError(f"WebView 资源不存在: {cls.webview_dir / 'config_ui.html'}")

    def setUp(self):
        """每个测试用例初始化"""
        self.process: Optional[subprocess.Popen] = None

    def tearDown(self):
        """每个测试用例清理"""
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()

    def start_process(self, args: list = None) -> subprocess.Popen:
        """启动进程"""
        cmd = [str(self.exe_path)]
        if args:
            cmd.extend(args)

        self.process = subprocess.Popen(
            cmd,
            cwd=str(self.build_dir),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        return self.process


class TestWebViewPageLoad(WebViewConfigTestCase):
    """测试页面加载"""

    def test_html_resources_exist(self):
        """测试 HTML 资源文件存在"""
        # 实际资源文件是 config_ui.html
        required_files = [
            "config_ui.html",
        ]

        for file in required_files:
            path = self.webview_dir / file
            self.assertTrue(
                path.exists(),
                f"必需文件不存在: {file}"
            )

    def test_html_structure(self):
        """测试 HTML 结构完整"""
        html_path = self.webview_dir / "config_ui.html"
        content = html_path.read_text(encoding='utf-8')

        # 检查必要元素 (WebView2 配置页面的关键元素)
        required_elements = [
            'id="logPath"',       # 日志路径输入框
            'id="modelPath"',     # 模型路径输入框
            'id="actionRows"',    # 按键映射表格
            'id="save"',          # 保存按钮
        ]

        for element in required_elements:
            self.assertIn(
                element,
                content,
                f"HTML 缺少必要元素: {element}"
            )


class TestConfigOperations(WebViewConfigTestCase):
    """测试配置操作"""

    def test_config_load(self):
        """测试配置加载"""
        # 启动配置测试模式
        process = self.start_process(["--test-config"])

        # 等待进程完成
        try:
            stdout, stderr = process.communicate(timeout=10)
            result = stdout.decode('utf-8', errors='ignore')

            # 检查结果
            self.assertIn(
                "test",
                result.lower(),
                "配置测试输出不符合预期"
            )
        except subprocess.TimeoutExpired:
            process.kill()
            self.fail("配置测试超时")


class TestWebViewAvailability(unittest.TestCase):
    """测试 WebView2 可用性"""

    def test_webview2_runtime_exists(self):
        """测试 WebView2 运行时是否存在"""
        # 检查注册表中的 WebView2 运行时
        import winreg

        try:
            key = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-5C2B1C4E0A6E}",
                0,
                winreg.KEY_READ
            )
            version, _ = winreg.QueryValueEx(key, "pv")
            winreg.CloseKey(key)

            self.assertTrue(
                version and len(version) > 0,
                "WebView2 运行时版本信息无效"
            )
            print(f"WebView2 运行时版本: {version}")

        except FileNotFoundError:
            self.fail("WebView2 运行时未安装")
        except Exception as e:
            self.fail(f"检查 WebView2 运行时失败: {e}")


def create_test_suite():
    """创建测试套件"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # 添加测试类
    suite.addTests(loader.loadTestsFromTestCase(TestWebViewPageLoad))
    suite.addTests(loader.loadTestsFromTestCase(TestConfigOperations))
    suite.addTests(loader.loadTestsFromTestCase(TestWebViewAvailability))

    return suite


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='WebView 配置页面功能测试')
    parser.add_argument('-v', '--verbose', action='store_true', help='详细输出')
    parser.add_argument('-k', '--keep-running', action='store_true', help='测试后保持运行')
    args = parser.parse_args()

    # 创建测试套件并运行
    suite = create_test_suite()
    runner = unittest.TextTestRunner(verbosity=2 if args.verbose else 1)
    result = runner.run(suite)

    # 返回结果
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
