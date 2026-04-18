#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PowerCapslock OCR selection test suite.

This suite tests the OCR selection functionality including:
- OCR data model tests
- OCR layout calculation tests
- OCR hit-test tests
- OCR selection tests
- OCR copy text tests
- OCR async tests
- End-to-end mock OCR tests
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass
class CheckResult:
    name: str
    passed: bool
    details: str


class OcrSelectionTestSuite:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.root = Path(__file__).resolve().parents[1]
        self.build_dir = self.root / "build"
        self.exe = self.build_dir / "powercapslock.exe"
        self.user_profile = Path(os.environ.get("USERPROFILE", str(Path.home())))

        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        self.output_dir = self.root / "test" / "output" / f"ocr_selection_{stamp}"
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
        """Ensure executable exists and no other instances are running."""
        if not self.exe.exists():
            raise FileNotFoundError(f"Executable not found: {self.exe}")
        running = self.find_running_instances()
        if not running:
            return
        if self.args.stop_existing:
            for pid in running:
                subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True, text=True)
            self.log(f"Stopped existing powercapslock.exe instances: {running}")
            time.sleep(0.5)
            return
        raise RuntimeError(
            "powercapslock.exe is already running. Close it first or pass --stop-existing."
        )

    @staticmethod
    def find_running_instances() -> list[int]:
        """Find all running powercapslock.exe process IDs."""
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

    def run_command(
        self, args: list[str], name: str, timeout: int = 30
    ) -> subprocess.CompletedProcess[str]:
        """Run a test command and capture output."""
        # Ensure no running instances before test
        running = self.find_running_instances()
        if running:
            if self.args.stop_existing:
                for pid in running:
                    subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True, text=True)
                time.sleep(0.5)
            else:
                raise RuntimeError(f"powercapslock.exe running before test: {running}")

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

    def parse_test_output(self, stdout: str) -> tuple[int, int]:
        """Parse test output to get passed/failed counts.

        Returns:
            tuple of (passed, failed)
        """
        passed = 0
        failed = 0
        for line in stdout.split("\n"):
            if "passed" in line.lower() and "failed" in line.lower():
                # Look for pattern like "X passed, Y failed"
                import re
                match = re.search(r"(\d+)\s+passed.*?(\d+)\s+failed", line, re.IGNORECASE)
                if match:
                    passed = int(match.group(1))
                    failed = int(match.group(2))
                else:
                    match = re.search(r"Total:\s*(\d+)\s+passed,\s*(\d+)\s+failed", line, re.IGNORECASE)
                    if match:
                        passed = int(match.group(1))
                        failed = int(match.group(2))
        return passed, failed

    def test_ocr_data_model(self) -> None:
        """Test OCR data model structures."""
        self.log("Running --test-ocr-data-model...")
        result = self.run_command(["--test-ocr-data-model"], "ocr_data_model")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR Data Model",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_ocr_layout(self) -> None:
        """Test OCR layout calculation."""
        self.log("Running --test-ocr-layout...")
        result = self.run_command(["--test-ocr-layout"], "ocr_layout")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR Layout Calculation",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_ocr_hit_test(self) -> None:
        """Test OCR hit test functionality."""
        self.log("Running --test-ocr-hit-test...")
        result = self.run_command(["--test-ocr-hit-test"], "ocr_hit_test")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR Hit Test",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_ocr_selection(self) -> None:
        """Test OCR selection state management."""
        self.log("Running --test-ocr-selection...")
        result = self.run_command(["--test-ocr-selection"], "ocr_selection")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR Selection",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_ocr_copy_text(self) -> None:
        """Test OCR text copying."""
        self.log("Running --test-ocr-copy-text...")
        result = self.run_command(["--test-ocr-copy-text"], "ocr_copy_text")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR Copy Text",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_ocr_float_ui(self) -> None:
        """Test OCR float UI placeholder."""
        self.log("Running --test-ocr-float-ui...")
        result = self.run_command(["--test-ocr-float-ui"], "ocr_float_ui")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR Float UI",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_ocr_async(self) -> None:
        """Test OCR async infrastructure."""
        self.log("Running --test-ocr-async...")
        result = self.run_command(["--test-ocr-async"], "ocr_async")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR Async",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_ocr_e2e_mock(self) -> None:
        """Test end-to-end OCR with mock data."""
        self.log("Running --test-ocr-e2e-mock...")
        result = self.run_command(["--test-ocr-e2e-mock"], "ocr_e2e_mock")
        passed, failed = self.parse_test_output(result.stdout)
        ok = result.returncode == 0 and failed == 0
        self.add_result(
            "OCR E2E Mock",
            ok,
            f"rc={result.returncode}, passed={passed}, failed={failed}"
        )

    def test_all(self) -> None:
        """Run all OCR selection tests."""
        self.log("Running all OCR selection tests...")

        # Run all individual tests
        self.test_ocr_data_model()
        self.test_ocr_layout()
        self.test_ocr_hit_test()
        self.test_ocr_selection()
        self.test_ocr_copy_text()
        self.test_ocr_float_ui()
        self.test_ocr_async()
        self.test_ocr_e2e_mock()

        # Summary
        total = len(self.results)
        passed = sum(1 for r in self.results if r.passed)
        failed = total - passed

        self.log(f"\n=== OCR Selection Test Summary ===")
        self.log(f"Total: {total} tests")
        self.log(f"Passed: {passed}")
        self.log(f"Failed: {failed}")

        # Write report
        self.write_report(passed, failed, total)

        # Write JSON result
        import json
        self.result_path.write_text(
            json.dumps(
                {
                    "passed": passed,
                    "failed": failed,
                    "total": total,
                    "results": [
                        {"name": r.name, "passed": r.passed, "details": r.details}
                        for r in self.results
                    ],
                },
                indent=2,
            ),
            encoding="utf-8"
        )

    def write_report(self, passed: int, failed: int, total: int) -> None:
        """Write markdown report."""
        lines = [
            "# OCR Selection Test Report",
            "",
            f"**Date**: {dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
            "",
            f"**Executable**: {self.exe}",
            "",
            "## Summary",
            "",
            f"- **Total**: {total} tests",
            f"- **Passed**: {passed}",
            f"- **Failed**: {failed}",
            "",
            "## Results",
            "",
        ]

        for r in self.results:
            status = "PASS" if r.passed else "FAIL"
            lines.append(f"- **{status}**: {r.name} - {r.details}")

        lines.extend(["", "## Details", ""])

        for r in self.results:
            lines.append(f"### {r.name}")
            lines.append(f"```\n{r.details}\n```")
            lines.append("")

        self.report_path.write_text("\n".join(lines), encoding="utf-8")
        self.log(f"Report written to: {self.report_path}")

    def run(self) -> int:
        """Main entry point."""
        self.log(f"OCR Selection Test Suite starting")
        self.log(f"Executable: {self.exe}")

        try:
            self.ensure_ready()
        except RuntimeError as e:
            self.log(f"ERROR: {e}")
            return 1

        try:
            if self.args.test_name:
                # Run specific test
                test_method = getattr(self, f"test_{self.args.test_name}", None)
                if test_method is None:
                    self.log(f"Unknown test: {self.args.test_name}")
                    return 1
                test_method()
            else:
                # Run all tests
                self.test_all()

            return 0 if all(r.passed for r in self.results) else 1

        except Exception as e:
            self.log(f"ERROR: {e}")
            import traceback
            traceback.print_exc()
            return 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="PowerCapslock OCR selection test suite"
    )
    parser.add_argument(
        "--stop-existing",
        action="store_true",
        help="Stop existing powercapslock.exe instances before testing",
    )
    parser.add_argument(
        "--test-name",
        choices=[
            "ocr_data_model",
            "ocr_layout",
            "ocr_hit_test",
            "ocr_selection",
            "ocr_copy_text",
            "ocr_float_ui",
            "ocr_async",
            "ocr_e2e_mock",
        ],
        help="Run specific test instead of all tests",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Run all tests (default if no specific test is chosen)",
    )

    args = parser.parse_args()

    suite = OcrSelectionTestSuite(args)
    return suite.run()


if __name__ == "__main__":
    sys.exit(main())