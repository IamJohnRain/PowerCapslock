# Configuration Functional Tests

Run from the repository root after building:

```powershell
python test\config_function_test.py
```

If a development copy of `powercapslock.exe` is already running, close it first
or pass:

```powershell
python test\config_function_test.py --stop-existing
```

The suite backs up the real user config at
`%USERPROFILE%\.PowerCapslock\config\config.json`, writes isolated test values,
and restores the original config at the end. Use `--keep-config` only when you
want to inspect the generated test config manually.

The GUI button test uses `pywin32` (`win32gui`, `win32api`, `win32process`) to
drive the native Windows dialogs.

Covered checks:

- log directory changes are consumed after a normal application restart, and a
  log file is created in the new directory;
- model directory changes are loaded and saved by the application config path;
- key mappings can be added, edited, and deleted, with behavior verified through
  the test-only `--test-key-mapping` application mode.
- the configuration dialog GUI responds to the log Browse button, Add/Edit
  mapping forms, Remove confirmation, path edits, and OK save.

Outputs are written under `test\output\config_function_<timestamp>\`:

- `report.md` for the human-readable summary;
- `result.json` for machine-readable results;
- `run.log` plus command stdout/stderr captures for debugging.
