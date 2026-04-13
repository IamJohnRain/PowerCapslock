#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PowerCapslock screenshot functional test suite.

This suite tests the screenshot functionality including:
- Screenshot capture
- Image save
- Clipboard copy
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class CheckResult:
    name: str
    passed: bool
    details: str


class ScreenshotFunctionSuite:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.root = Path(__file__).resolve().parents[1]
        self.build_dir = self.root / "build"
        self.exe = self.build_dir / "powercapslock.exe"
        self.user_profile = Path(os.environ.get("USERPROFILE", str(Path.home())))

        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        self.output_dir = self.root / "test" / "output" / f"screenshot_{stamp}"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.run_log = self.output_dir / "run.log"
        self.report_path = self.output_dir / "report.md"
        self.result_path = self.output_dir / "result.json"
        self.results: list[CheckResult] = []

    def log(self, message: str) -> None:
        line = f"[{dt.datetime.now().strftime('%H:%M:%S')}] {message}"
        print(line)
        with self.run_log.open("a", encoding="utf-8") as f:
            f.write(line + "\n")

    def add_result(self, name: str, passed: bool, details: str) -> None:
        status = "PASS" if passed else "FAIL"
        self.results.append(CheckResult(name, passed, details))
        self.log(f"{status}: {name} - {details}")

    def ensure_ready(self) -> None:
        if not self.exe.exists():
            raise FileNotFoundError(f"Executable not found: {self.exe}")
        running = self.find_running_instances()
        if not running:
            return
        if self.args.stop_existing:
            for pid in running:
                subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True, text=True)
            self.log(f"Stopped existing powercapslock.exe instances: {running}")
            return
        raise RuntimeError(
            "powercapslock.exe is already running. Close it first or pass --stop-existing."
        )

    @staticmethod
    def find_running_instances() -> list[int]:
        import csv
        import io
        result = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq powercapslock.exe", "/FO", "CSV", "/NH"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            return []
        pids: list[int] = []
        for row in csv.reader(io.StringIO(result.stdout)):
            if len(row) >= 2 and row[0].lower() == "powercapslock.exe":
                try:
                    pids.append(int(row[1]))
                except ValueError:
                    pass
        return pids

    def run_command(self, args: list[str], name: str, timeout: int = 20) -> subprocess.CompletedProcess[str]:
        stdout_path = self.output_dir / f"{name}_stdout.txt"
        stderr_path = self.output_dir / f"{name}_stderr.txt"
        result = subprocess.run(
            [str(self.exe), *args],
            cwd=str(self.build_dir),
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        stdout_path.write_text(result.stdout or "", encoding="utf-8", errors="replace")
        stderr_path.write_text(result.stderr or "", encoding="utf-8", errors="replace")
        return result

    def test_capture_rect(self) -> None:
        """测试截图捕获"""
        output_file = self.output_dir / "capture_test.bmp"
        result = self.run_command(
            ["--test-screenshot-capture", "0", "0", "200", "200", str(output_file)],
            "capture_rect",
        )
        passed = result.returncode == 0 and output_file.exists() and output_file.stat().st_size > 0
        self.add_result("截图捕获", passed, f"rc={result.returncode}, file_exists={output_file.exists()}")

    def test_save(self) -> None:
        """测试保存功能"""
        output_file = self.output_dir / "save_test.bmp"
        result = self.run_command(
            ["--test-screenshot-save", str(output_file)],
            "save",
        )
        passed = result.returncode == 0 and output_file.exists() and output_file.stat().st_size > 0
        self.add_result("图像保存", passed, f"rc={result.returncode}, file_exists={output_file.exists()}")

    def test_clipboard(self) -> None:
        """测试剪贴板功能"""
        result = self.run_command(
            ["--test-screenshot-clipboard"],
            "clipboard",
        )
        passed = result.returncode == 0
        self.add_result("剪贴板复制", passed, f"rc={result.returncode}")

    def test_all(self) -> None:
        """测试全量测试命令"""
        result = self.run_command(
            ["--test-screenshot-all"],
            "all",
        )
        passed = result.returncode == 0
        self.add_result("全量测试", passed, f"rc={result.returncode}")

    def test_overlay(self) -> None:
        """测试选区窗口"""
        result = self.run_command(
            ["--test-screenshot-overlay"],
            "overlay",
        )
        passed = result.returncode == 0
        self.add_result("选区窗口", passed, f"rc={result.returncode}")

    def test_toolbar(self) -> None:
        """测试工具栏菜单"""
        result = self.run_command(
            ["--test-screenshot-toolbar"],
            "toolbar",
        )
        passed = result.returncode == 0
        self.add_result("工具栏菜单", passed, f"rc={result.returncode}")

    def test_float(self) -> None:
        """测试浮动窗口"""
        result = self.run_command(
            ["--test-screenshot-float"],
            "float",
        )
        passed = result.returncode == 0
        passed = passed and (self.output_dir / "float_stdout.txt").exists()
        passed = passed and (self.output_dir / "float_stderr.txt").exists()
        self.add_result("浮动窗口", passed, f"rc={result.returncode}")

    def test_annotate(self) -> None:
        """测试标注功能"""
        result = self.run_command(
            ["--test-screenshot-annotate"],
            "annotate",
        )
        passed = result.returncode == 0
        self.add_result("标注功能", passed, f"rc={result.returncode}")

    def test_ocr(self) -> None:
        """测试OCR功能"""
        result = self.run_command(
            ["--test-screenshot-ocr"],
            "ocr",
        )
        passed = result.returncode == 0
        self.add_result("OCR识别", passed, f"rc={result.returncode}")

    def test_config(self) -> None:
        """测试配置加载和截图动作"""
        result = self.run_command(
            ["--test-config"],
            "config",
        )
        passed = result.returncode == 0
        # 检查配置输出中包含 actions
        has_actions = "actions" in (result.stdout or "").lower()
        self.add_result("配置加载", passed, f"rc={result.returncode}, has_actions={has_actions}")

    def write_reports(self) -> None:
        passed_count = sum(1 for r in self.results if r.passed)
        result_data = {
            "passed": passed_count,
            "total": len(self.results),
            "output_dir": str(self.output_dir),
            "results": [r.__dict__ for r in self.results],
        }
        self.result_path.write_text(
            json.dumps(result_data, indent=2, ensure_ascii=False),
            encoding="utf-8",
        )

        lines = [
            "# PowerCapslock Screenshot Functional Test Report",
            "",
            f"- Time: {dt.datetime.now().isoformat(timespec='seconds')}",
            f"- Executable: `{self.exe}`",
            f"- Result: {passed_count}/{len(self.results)} passed",
            "",
            "## Checks",
            "",
        ]
        for item in self.results:
            status = "PASS" if item.passed else "FAIL"
            lines.append(f"- **{status}** {item.name}: {item.details}")
        lines.extend(["", f"JSON result: `{self.result_path}`", f"Run log: `{self.run_log}`", ""])
        self.report_path.write_text("\n".join(lines), encoding="utf-8")

    def run(self) -> bool:
        self.ensure_ready()
        try:
            self.test_capture_rect()
            self.test_save()
            self.test_clipboard()
            self.test_all()
            self.test_overlay()
            self.test_toolbar()
            self.test_float()
            self.test_annotate()
            self.test_ocr()
            self.test_config()
        finally:
            self.write_reports()

        passed_count = sum(1 for r in self.results if r.passed)
        self.log(f"Report: {self.report_path}")
        self.log(f"Summary: {passed_count}/{len(self.results)} passed")
        return passed_count == len(self.results)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run PowerCapslock screenshot functional tests.")
    parser.add_argument(
        "--stop-existing",
        action="store_true",
        help="Stop existing powercapslock.exe instances before running tests.",
    )
    return parser.parse_args()


def main() -> int:
    suite = ScreenshotFunctionSuite(parse_args())
    try:
        return 0 if suite.run() else 1
    except Exception as exc:
        suite.log(f"ERROR: {exc}")
        try:
            suite.write_reports()
        finally:
            return 2


if __name__ == "__main__":
    sys.exit(main())
