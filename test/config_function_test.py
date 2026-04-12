#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PowerCapslock configuration functional test suite.

The suite intentionally exercises the real per-user config file because the
application currently hard-codes that location through SHGetFolderPath().
It backs the file up before the first write and restores it at the end unless
--keep-config is passed.
"""

from __future__ import annotations

import argparse
import copy
import csv
import ctypes
import datetime as dt
import io
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_MAPPINGS = [
    {"from": "H", "to": "LEFT"},
    {"from": "J", "to": "DOWN"},
    {"from": "K", "to": "UP"},
    {"from": "L", "to": "RIGHT"},
    {"from": "I", "to": "HOME"},
    {"from": "O", "to": "END"},
    {"from": "U", "to": "PAGEDOWN"},
    {"from": "P", "to": "PAGEUP"},
    {"from": "1", "to": "F1"},
    {"from": "2", "to": "F2"},
    {"from": "3", "to": "F3"},
    {"from": "4", "to": "F4"},
    {"from": "5", "to": "F5"},
    {"from": "6", "to": "F6"},
    {"from": "7", "to": "F7"},
    {"from": "8", "to": "F8"},
    {"from": "9", "to": "F9"},
    {"from": "0", "to": "F10"},
    {"from": "MINUS", "to": "F11"},
    {"from": "EQUAL", "to": "F12"},
]


@dataclass
class CheckResult:
    name: str
    passed: bool
    details: str


class ConfigFunctionSuite:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.root = Path(__file__).resolve().parents[1]
        self.build_dir = self.root / "build"
        self.exe = self.build_dir / "powercapslock.exe"
        self.user_profile = Path(os.environ.get("USERPROFILE", str(Path.home())))
        self.config_path = self.user_profile / ".PowerCapslock" / "config" / "config.json"
        self.default_log_dir = self.user_profile / ".PowerCapslock" / "logs"

        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        self.output_dir = self.root / "test" / "output" / f"config_function_{stamp}"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.run_log = self.output_dir / "run.log"
        self.report_path = self.output_dir / "report.md"
        self.result_path = self.output_dir / "result.json"
        self.backup_path = self.output_dir / "original_config.json"
        self.config_existed = False
        self.results: list[CheckResult] = []

    def log(self, message: str) -> None:
        line = f"[{dt.datetime.now().strftime('%H:%M:%S')}] {message}"
        print(line)
        with self.run_log.open("a", encoding="utf-8") as f:
            f.write(line + "\n")

    @staticmethod
    def path_for_config(path: Path) -> str:
        # The C config parser does not unescape JSON backslashes yet. Forward
        # slashes avoid that parser limitation while remaining valid on Windows.
        return path.resolve().as_posix()

    def base_config(self, log_dir: Path, model_dir: Path) -> dict[str, Any]:
        return {
            "version": "1.0",
            "modifier": {
                "key": "CAPSLOCK",
                "suppress_original": True,
                "control_led": False,
            },
            "mappings": copy.deepcopy(DEFAULT_MAPPINGS),
            "options": {
                "start_enabled": True,
                "log_level": "DEBUG",
                "log_to_file": True,
                "keyboard_layout": "auto",
            },
            "paths": {
                "log_directory": self.path_for_config(log_dir),
                "model_directory": self.path_for_config(model_dir),
            },
            "voice_input": {
                "enabled": False,
                "asked": True,
            },
        }

    def write_config(self, config: dict[str, Any]) -> None:
        self.config_path.parent.mkdir(parents=True, exist_ok=True)
        with self.config_path.open("w", encoding="utf-8") as f:
            json.dump(config, f, indent=4, ensure_ascii=False)
            f.write("\n")

    def read_config(self) -> dict[str, Any]:
        with self.config_path.open("r", encoding="utf-8") as f:
            return json.load(f)

    def backup_config(self) -> None:
        self.config_existed = self.config_path.exists()
        if self.config_existed:
            shutil.copy2(self.config_path, self.backup_path)
            self.log(f"Backed up config: {self.backup_path}")
        else:
            self.log("No existing config found; test will remove generated config on restore.")

    def restore_config(self) -> None:
        if self.args.keep_config:
            self.log("--keep-config was set; leaving test config in place.")
            return
        if self.config_existed:
            self.config_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.backup_path, self.config_path)
            self.log("Restored original config.")
        elif self.config_path.exists():
            self.config_path.unlink()
            self.log("Removed config created by the test.")

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

    def run_command_mode(self, args: list[str], name: str, timeout: int = 20) -> subprocess.CompletedProcess[str]:
        stdout_path = self.output_dir / f"{name}_stdout.txt"
        stderr_path = self.output_dir / f"{name}_stderr.txt"
        result = subprocess.run(
            [str(self.exe), *args],
            cwd=str(self.build_dir),
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        stdout_path.write_text(result.stdout or "", encoding="utf-8", errors="replace")
        stderr_path.write_text(result.stderr or "", encoding="utf-8", errors="replace")
        return result

    def start_config_dialog(self, env: dict[str, str] | None = None) -> subprocess.Popen[Any]:
        process_env = os.environ.copy()
        if env:
            process_env.update(env)
        return subprocess.Popen(
            [str(self.exe), "--test-config-dialog"],
            cwd=str(self.build_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=process_env,
        )

    def launch_normal_for_log(self, seconds: float = 3.0) -> subprocess.Popen[Any]:
        return subprocess.Popen(
            [str(self.exe)],
            cwd=str(self.build_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def stop_process(self, proc: subprocess.Popen[Any]) -> None:
        if proc.poll() is not None:
            return
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)

    def today_log_file(self, log_dir: Path) -> Path:
        return log_dir / f"powercapslock_{dt.datetime.now().strftime('%Y%m%d')}.log"

    def test_log_path_restart(self) -> None:
        log_dir = self.output_dir / "log_path" / "new_logs"
        model_dir = self.root / "models"
        log_dir.mkdir(parents=True, exist_ok=True)
        config = self.base_config(log_dir, model_dir)
        self.write_config(config)

        expected_log = self.today_log_file(log_dir)
        if expected_log.exists():
            expected_log.unlink()

        proc = self.launch_normal_for_log()
        try:
            deadline = time.time() + 6
            while time.time() < deadline:
                if expected_log.exists() and expected_log.stat().st_size > 0:
                    break
                if proc.poll() is not None:
                    break
                time.sleep(0.2)
        finally:
            self.stop_process(proc)

        passed = expected_log.exists() and expected_log.stat().st_size > 0
        details = f"expected log at {expected_log}"
        self.add_result("log path survives restart and receives logs", passed, details)

    def test_model_path_persistence(self) -> None:
        log_dir = self.output_dir / "model_path" / "logs"
        model_dir = self.output_dir / "model_path" / "alternate_models"
        log_dir.mkdir(parents=True, exist_ok=True)
        model_dir.mkdir(parents=True, exist_ok=True)

        config = self.base_config(log_dir, model_dir)
        self.write_config(config)

        result = self.run_command_mode(["--test-config"], "model_path_config")
        saved = self.read_config()
        actual = saved.get("paths", {}).get("model_directory")
        expected = self.path_for_config(model_dir)
        passed = result.returncode == 0 and actual == expected
        details = f"expected model_directory={expected}, actual={actual}, rc={result.returncode}"
        self.add_result("model path is loaded and saved by config mode", passed, details)

    def run_key_mapping_probe(self, from_key: str, to_key: str, name: str) -> tuple[bool, dict[str, Any]]:
        report = self.output_dir / f"{name}.json"
        result = self.run_command_mode(
            ["--test-key-mapping", from_key, to_key, self.path_for_config(report)],
            name,
        )
        data: dict[str, Any] = {}
        if report.exists():
            data = json.loads(report.read_text(encoding="utf-8"))
        passed = result.returncode == 0 and bool(data.get("passed"))
        return passed, data

    def test_key_mapping_add_edit_delete(self) -> None:
        log_dir = self.output_dir / "key_mapping" / "logs"
        model_dir = self.root / "models"
        log_dir.mkdir(parents=True, exist_ok=True)

        config = self.base_config(log_dir, model_dir)
        config["mappings"].append({"from": "Q", "to": "B"})
        self.write_config(config)
        add_passed, add_report = self.run_key_mapping_probe("Q", "B", "keymap_add_q_b")
        self.add_result(
            "key mapping add takes effect",
            add_passed,
            f"Q->B probe report: {add_report}",
        )

        for mapping in config["mappings"]:
            if mapping["from"] == "Q":
                mapping["to"] = "C"
        self.write_config(config)
        edit_passed, edit_report = self.run_key_mapping_probe("Q", "C", "keymap_edit_q_c")
        self.add_result(
            "key mapping edit takes effect",
            edit_passed,
            f"Q->C probe report: {edit_report}",
        )

        config["mappings"] = [m for m in config["mappings"] if m["from"] != "Q"]
        self.write_config(config)
        delete_passed, delete_report = self.run_key_mapping_probe("Q", "NONE", "keymap_delete_q")
        saved = self.read_config()
        q_still_present = any(m.get("from") == "Q" for m in saved.get("mappings", []))
        passed = delete_passed and not q_still_present
        self.add_result(
            "key mapping delete fully removes behavior and config entry",
            passed,
            f"delete probe report: {delete_report}, q_still_present={q_still_present}",
        )

    @staticmethod
    def wait_until(predicate, timeout: float, description: str) -> Any:
        deadline = time.time() + timeout
        while time.time() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(0.05)
        raise TimeoutError(description)

    def legacy_native_config_dialog_gui_buttons(self) -> None:
        try:
            import win32api
            import win32con
            import win32gui
            import win32process
        except ImportError as exc:
            self.add_result("config dialog GUI buttons and mapping form", False, f"pywin32 is required: {exc}")
            return

        user32 = ctypes.windll.user32
        user32.SendMessageW.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint,
            ctypes.c_size_t,
            ctypes.c_ssize_t,
        ]
        user32.SendMessageW.restype = ctypes.c_ssize_t

        IDC_TAB = 201
        IDC_LOG_PATH = 202
        IDC_BROWSE_LOG = 203
        IDC_MODEL_PATH = 204
        IDC_MAPPING_LIST = 206
        IDC_BTN_ADD = 207
        IDC_BTN_EDIT = 208
        IDC_BTN_REMOVE = 209
        IDOK = 1
        IDYES = 6
        BM_CLICK = 0x00F5
        LVM_FIRST = 0x1000
        LVM_GETITEMCOUNT = LVM_FIRST + 4
        LVM_ENSUREVISIBLE = LVM_FIRST + 19

        def top_windows_for_pid(pid: int) -> list[int]:
            handles: list[int] = []

            def callback(hwnd: int, _extra: object) -> bool:
                if win32gui.IsWindowVisible(hwnd):
                    _, window_pid = win32process.GetWindowThreadProcessId(hwnd)
                    if window_pid == pid:
                        handles.append(hwnd)
                return True

            win32gui.EnumWindows(callback, None)
            return handles

        def find_window(pid: int, title: str) -> int:
            for hwnd in top_windows_for_pid(pid):
                if win32gui.GetWindowText(hwnd) == title:
                    return hwnd
            return 0

        def find_extra_dialog(pid: int, exclude: set[int]) -> int:
            for hwnd in top_windows_for_pid(pid):
                if hwnd in exclude:
                    continue
                if win32gui.GetClassName(hwnd) == "#32770":
                    return hwnd
            return 0

        def get_item(dialog: int, control_id: int) -> int:
            try:
                return win32gui.GetDlgItem(dialog, control_id)
            except Exception:
                return 0

        def click(dialog: int, control_id: int) -> None:
            hwnd = self.wait_until(lambda: get_item(dialog, control_id), 5, f"control {control_id} not found")
            win32gui.SendMessage(hwnd, BM_CLICK, 0, 0)

        def post_click(dialog: int, control_id: int) -> None:
            hwnd = self.wait_until(lambda: get_item(dialog, control_id), 5, f"control {control_id} not found")
            win32gui.PostMessage(hwnd, BM_CLICK, 0, 0)

        def post_click_handle(hwnd: int) -> None:
            if not hwnd or not win32gui.IsWindow(hwnd):
                raise RuntimeError(f"button handle is not valid: {hwnd}")
            win32gui.PostMessage(hwnd, BM_CLICK, 0, 0)

        def switch_mapping_tab(dialog: int) -> None:
            tab = self.wait_until(lambda: get_item(dialog, IDC_TAB), 5, "tab control not found")
            win32gui.PostMessage(tab, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, (15 << 16) | 180)
            win32gui.PostMessage(tab, win32con.WM_LBUTTONUP, 0, (15 << 16) | 180)
            self.wait_until(
                lambda: get_item(dialog, IDC_BTN_ADD)
                and get_item(dialog, IDC_BTN_EDIT)
                and get_item(dialog, IDC_BTN_REMOVE),
                5,
                "mapping tab controls not created",
            )

        def select_last_mapping(dialog: int) -> int:
            list_hwnd = self.wait_until(lambda: get_item(dialog, IDC_MAPPING_LIST), 5, "mapping list not found")
            count = win32gui.SendMessage(list_hwnd, LVM_GETITEMCOUNT, 0, 0)
            if count <= 0:
                raise RuntimeError("mapping list is empty")
            index = count - 1
            user32.SendMessageW(list_hwnd, LVM_ENSUREVISIBLE, index, 0)
            left, top, _right, _bottom = win32gui.GetWindowRect(list_hwnd)
            x = left + 20
            y = top + 30 + min(index, 6) * 18
            win32gui.SetForegroundWindow(dialog)
            win32api.SetCursorPos((x, y))
            win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
            win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
            time.sleep(0.15)
            return index

        def open_dialog() -> tuple[subprocess.Popen[Any], int, int]:
            proc = self.start_config_dialog()
            dialog = self.wait_until(
                lambda: find_window(proc.pid, "PowerCapslock 设置"),
                8,
                "config dialog did not open",
            )
            self.wait_until(lambda: get_item(dialog, IDC_LOG_PATH), 5, "general tab controls not created")
            return proc, proc.pid, dialog

        def close_process(proc: subprocess.Popen[Any]) -> None:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3)

        def set_text(dialog: int, control_id: int, value: str) -> None:
            hwnd = self.wait_until(lambda: get_item(dialog, control_id), 5, f"edit {control_id} not found")
            win32gui.SendMessage(hwnd, win32con.WM_SETTEXT, 0, value)

        def mapping_dialog(pid: int, from_key: str, to_key: str) -> None:
            dlg = self.wait_until(lambda: find_window(pid, "按键映射"), 5, "mapping dialog did not open")
            set_text(dlg, 301, from_key)
            set_text(dlg, 302, to_key)
            click(dlg, IDOK)
            self.wait_until(lambda: not win32gui.IsWindow(dlg), 5, "mapping dialog did not close")

        def save_dialog(dialog: int, proc: subprocess.Popen[Any]) -> None:
            click(dialog, IDOK)
            proc.wait(timeout=8)

        log_dir = self.output_dir / "gui_dialog" / "logs"
        model_dir = self.output_dir / "gui_dialog" / "models"
        edited_log_dir = self.output_dir / "gui_dialog" / "edited_logs"
        edited_model_dir = self.output_dir / "gui_dialog" / "edited_models"
        for directory in (log_dir, model_dir, edited_log_dir, edited_model_dir):
            directory.mkdir(parents=True, exist_ok=True)

        config = self.base_config(log_dir, model_dir)
        config["mappings"] = [{"from": "H", "to": "LEFT"}]
        self.write_config(config)

        browse_opened = False
        add_saved = False
        edit_saved = False
        remove_saved = False
        proc: subprocess.Popen[Any] | None = None

        try:
            proc, pid, dialog = open_dialog()
            post_click(dialog, IDC_BROWSE_LOG)
            folder_dialog = self.wait_until(
                lambda: find_extra_dialog(pid, {dialog}),
                5,
                "log Browse button did not open a folder dialog",
            )
            browse_opened = True
            win32gui.SendMessage(folder_dialog, win32con.WM_CLOSE, 0, 0)
            self.wait_until(lambda: not win32gui.IsWindow(folder_dialog), 5, "folder dialog did not close")

            set_text(dialog, IDC_LOG_PATH, self.path_for_config(edited_log_dir))
            set_text(dialog, IDC_MODEL_PATH, self.path_for_config(edited_model_dir))
            switch_mapping_tab(dialog)
            post_click(dialog, IDC_BTN_ADD)
            mapping_dialog(pid, "Q", "B")
            save_dialog(dialog, proc)
            proc = None

            saved = self.read_config()
            add_saved = (
                saved.get("paths", {}).get("log_directory") == self.path_for_config(edited_log_dir)
                and saved.get("paths", {}).get("model_directory") == self.path_for_config(edited_model_dir)
                and any(m.get("from") == "Q" and m.get("to") == "B" for m in saved.get("mappings", []))
            )

            proc, pid, dialog = open_dialog()
            switch_mapping_tab(dialog)
            edit_button = get_item(dialog, IDC_BTN_EDIT)
            select_last_mapping(dialog)
            post_click_handle(edit_button)
            mapping_dialog(pid, "Q", "C")
            save_dialog(dialog, proc)
            proc = None

            saved = self.read_config()
            edit_saved = any(m.get("from") == "Q" and m.get("to") == "C" for m in saved.get("mappings", []))

            proc, pid, dialog = open_dialog()
            switch_mapping_tab(dialog)
            remove_button = get_item(dialog, IDC_BTN_REMOVE)
            select_last_mapping(dialog)
            post_click_handle(remove_button)
            confirm = self.wait_until(lambda: find_window(pid, "确认删除"), 5, "delete confirmation did not open")
            click(confirm, IDYES)
            self.wait_until(lambda: not win32gui.IsWindow(confirm), 5, "delete confirmation did not close")
            save_dialog(dialog, proc)
            proc = None

            saved = self.read_config()
            remove_saved = not any(m.get("from") == "Q" for m in saved.get("mappings", []))

            passed = browse_opened and add_saved and edit_saved and remove_saved
            details = (
                f"browse_opened={browse_opened}, add_saved={add_saved}, "
                f"edit_saved={edit_saved}, remove_saved={remove_saved}"
            )
            self.add_result("config dialog GUI buttons and mapping form", passed, details)
        except Exception as exc:
            self.add_result(
                "config dialog GUI buttons and mapping form",
                False,
                (
                    f"{exc}; browse_opened={browse_opened}, add_saved={add_saved}, "
                    f"edit_saved={edit_saved}, remove_saved={remove_saved}"
                ),
            )
        finally:
            if proc is not None:
                close_process(proc)

    def run_config_dialog_webview_action(
        self,
        action: str,
        log_path: Path | None = None,
        model_path: Path | None = None,
    ) -> tuple[bool, str]:
        env = {
            "POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_ACTION": action,
            "POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_SKIP_MODEL_LOAD": "1",
        }
        if log_path is not None:
            env["POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_LOG_PATH"] = self.path_for_config(log_path)
        if model_path is not None:
            env["POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_MODEL_PATH"] = self.path_for_config(model_path)

        proc = self.start_config_dialog(env)
        try:
            rc = proc.wait(timeout=18)
        except subprocess.TimeoutExpired:
            self.stop_process(proc)
            return False, f"{action} timed out"
        return rc == 0, f"{action} rc={rc}"

    def test_config_dialog_gui_buttons(self) -> None:
        log_dir = self.output_dir / "gui_dialog" / "logs"
        model_dir = self.output_dir / "gui_dialog" / "models"
        edited_log_dir = self.output_dir / "gui_dialog" / "edited_logs"
        edited_model_dir = self.output_dir / "gui_dialog" / "edited_models"
        for directory in (log_dir, model_dir, edited_log_dir, edited_model_dir):
            directory.mkdir(parents=True, exist_ok=True)

        config = self.base_config(log_dir, model_dir)
        config["mappings"] = [{"from": "H", "to": "LEFT"}]
        self.write_config(config)

        add_ok, add_detail = self.run_config_dialog_webview_action("add", edited_log_dir, edited_model_dir)
        saved = self.read_config()
        add_saved = (
            saved.get("paths", {}).get("log_directory") == self.path_for_config(edited_log_dir)
            and saved.get("paths", {}).get("model_directory") == self.path_for_config(edited_model_dir)
            and any(m.get("from") == "Q" and m.get("to") == "B" for m in saved.get("mappings", []))
        )

        edit_ok, edit_detail = self.run_config_dialog_webview_action("edit")
        saved = self.read_config()
        edit_saved = any(m.get("from") == "Q" and m.get("to") == "C" for m in saved.get("mappings", []))

        remove_ok, remove_detail = self.run_config_dialog_webview_action("delete")
        saved = self.read_config()
        remove_saved = not any(m.get("from") == "Q" for m in saved.get("mappings", []))

        passed = add_ok and add_saved and edit_ok and edit_saved and remove_ok and remove_saved
        details = (
            f"{add_detail}, add_saved={add_saved}; "
            f"{edit_detail}, edit_saved={edit_saved}; "
            f"{remove_detail}, remove_saved={remove_saved}"
        )
        self.add_result("config WebView buttons and mapping form", passed, details)

    def write_reports(self) -> None:
        passed_count = sum(1 for r in self.results if r.passed)
        result_data = {
            "passed": passed_count,
            "total": len(self.results),
            "output_dir": str(self.output_dir),
            "config_path": str(self.config_path),
            "results": [r.__dict__ for r in self.results],
        }
        self.result_path.write_text(
            json.dumps(result_data, indent=2, ensure_ascii=False),
            encoding="utf-8",
        )

        lines = [
            "# PowerCapslock Configuration Functional Test Report",
            "",
            f"- Time: {dt.datetime.now().isoformat(timespec='seconds')}",
            f"- Executable: `{self.exe}`",
            f"- Config path: `{self.config_path}`",
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
        self.backup_config()
        try:
            self.test_log_path_restart()
            self.test_model_path_persistence()
            self.test_key_mapping_add_edit_delete()
            self.test_config_dialog_gui_buttons()
        finally:
            self.restore_config()
            self.write_reports()

        passed_count = sum(1 for r in self.results if r.passed)
        self.log(f"Report: {self.report_path}")
        self.log(f"Summary: {passed_count}/{len(self.results)} passed")
        return passed_count == len(self.results)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run PowerCapslock config functional tests.")
    parser.add_argument(
        "--keep-config",
        action="store_true",
        help="Do not restore the original user config after the run.",
    )
    parser.add_argument(
        "--stop-existing",
        action="store_true",
        help="Stop existing powercapslock.exe instances before running tests.",
    )
    return parser.parse_args()


def main() -> int:
    suite = ConfigFunctionSuite(parse_args())
    try:
        return 0 if suite.run() else 1
    except Exception as exc:
        suite.log(f"ERROR: {exc}")
        try:
            suite.restore_config()
            suite.write_reports()
        finally:
            return 2


if __name__ == "__main__":
    sys.exit(main())
