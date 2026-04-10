#!/usr/bin/env python3
"""Self-verify the CapsLock+A hook fix via built-in test mode and logs."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
TEST_DIR = REPO_ROOT / "test"
OUTPUT_DIR = TEST_DIR / "output"
EXE_PATH = REPO_ROOT / "build" / "powercapslock.exe"
LOG_DIR = Path(os.environ["USERPROFILE"]) / ".PowerCapslock" / "logs"

JSON_REPORT = OUTPUT_DIR / "capslock_a_self_test_result.json"
MARKDOWN_REPORT = OUTPUT_DIR / "capslock_a_self_test_report.md"
STDOUT_REPORT = OUTPUT_DIR / "capslock_a_self_test_stdout.txt"
LOG_EXCERPT = OUTPUT_DIR / "capslock_a_self_test_log_excerpt.txt"

LOG_ENCODINGS = ("utf-8", "gbk", "utf-16", "latin-1")


def read_text_with_fallback(path: Path) -> str:
    data = path.read_bytes()
    for encoding in LOG_ENCODINGS:
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def latest_log_file() -> Path | None:
    if not LOG_DIR.exists():
        return None

    log_files = [path for path in LOG_DIR.glob("*.log") if path.is_file()]
    if not log_files:
        return None

    return max(log_files, key=lambda path: path.stat().st_mtime)


def extract_run_log(log_text: str, run_id: int) -> list[str]:
    needle = f"run_id={run_id}"
    lines = [line for line in log_text.splitlines() if needle in line]

    if lines:
        return lines

    fallback_keywords = (
        "[CapsLock+A",
        "CapsLock+A",
        "async_start",
        "async_stop",
    )
    return [line for line in log_text.splitlines() if any(keyword in line for keyword in fallback_keywords)]


def render_markdown(
    process: subprocess.CompletedProcess[str],
    report: dict,
    log_file: Path | None,
    excerpt_lines: list[str],
    passed: bool,
) -> str:
    run_id = report.get("run_id", "unknown")
    lines = [
        "# CapsLock+A Self Test",
        "",
        f"- Result: {'PASS' if passed else 'FAIL'}",
        f"- Executable: `{EXE_PATH}`",
        f"- Exit code: `{process.returncode}`",
        f"- Run ID: `{run_id}`",
        f"- Log file: `{log_file}`" if log_file else "- Log file: `not found`",
        f"- JSON report: `{JSON_REPORT}`",
        "",
        "## Hook Report",
        "",
        f"- capslock_down_action: `{report.get('capslock_down_action')}`",
        f"- a_down_action: `{report.get('a_down_action')}`",
        f"- a_up_action: `{report.get('a_up_action')}`",
        f"- capslock_up_action: `{report.get('capslock_up_action')}`",
        f"- a_down_duration_ms: `{report.get('a_down_duration_ms')}`",
        f"- a_up_duration_ms: `{report.get('a_up_duration_ms')}`",
        f"- max_allowed_hook_duration_ms: `{report.get('max_allowed_hook_duration_ms')}`",
        f"- async_start_handled: `{report.get('async_start_handled')}`",
        f"- async_stop_handled: `{report.get('async_stop_handled')}`",
        f"- built_in_passed: `{report.get('passed')}`",
        "",
        "## Log Excerpt",
        "",
        "```text",
    ]

    if excerpt_lines:
        lines.extend(excerpt_lines[-40:])
    else:
        lines.append("<no matching log lines>")

    lines.extend(["```", ""])
    return "\n".join(lines)


def main() -> int:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    if not EXE_PATH.exists():
        message = f"Executable not found: {EXE_PATH}\n"
        STDOUT_REPORT.write_text(message, encoding="utf-8")
        print(message, end="")
        return 1

    command = [str(EXE_PATH), "--test-capslock-a", str(JSON_REPORT)]
    process = subprocess.run(
        command,
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
    )

    combined_output = (
        f"Command: {' '.join(command)}\n"
        f"Exit code: {process.returncode}\n\n"
        "[stdout]\n"
        f"{process.stdout}\n"
        "[stderr]\n"
        f"{process.stderr}\n"
    )
    STDOUT_REPORT.write_text(combined_output, encoding="utf-8")

    if not JSON_REPORT.exists():
        MARKDOWN_REPORT.write_text(
            "# CapsLock+A Self Test\n\n- Result: FAIL\n- Reason: JSON report was not produced.\n",
            encoding="utf-8",
        )
        print(f"FAIL: JSON report not found at {JSON_REPORT}")
        return 1

    report = json.loads(JSON_REPORT.read_text(encoding="utf-8"))
    log_file = latest_log_file()
    log_text = read_text_with_fallback(log_file) if log_file else ""
    excerpt_lines = extract_run_log(log_text, int(report["run_id"]))
    LOG_EXCERPT.write_text("\n".join(excerpt_lines) + ("\n" if excerpt_lines else ""), encoding="utf-8")

    log_begin = any("BEGIN" in line for line in excerpt_lines)
    log_end = any("END" in line for line in excerpt_lines)
    log_async_start = any("async_start handled" in line for line in excerpt_lines)
    log_async_stop = any("async_stop handled" in line for line in excerpt_lines)

    passed = (
        process.returncode == 0
        and report.get("passed") is True
        and report.get("capslock_down_action") == "intercept"
        and report.get("a_down_action") == "intercept"
        and report.get("a_up_action") == "intercept"
        and report.get("capslock_up_action") == "intercept"
        and report.get("async_start_handled") is True
        and report.get("async_stop_handled") is True
        and int(report.get("a_down_duration_ms", 0)) <= int(report.get("max_allowed_hook_duration_ms", 0))
        and int(report.get("a_up_duration_ms", 0)) <= int(report.get("max_allowed_hook_duration_ms", 0))
        and log_begin
        and log_end
        and log_async_start
        and log_async_stop
    )

    MARKDOWN_REPORT.write_text(
        render_markdown(process, report, log_file, excerpt_lines, passed),
        encoding="utf-8",
    )

    status = "PASS" if passed else "FAIL"
    print(f"{status}: CapsLock+A self-test")
    print(f"JSON report: {JSON_REPORT}")
    print(f"Markdown report: {MARKDOWN_REPORT}")
    print(f"Log excerpt: {LOG_EXCERPT}")
    print(f"Process output: {STDOUT_REPORT}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
