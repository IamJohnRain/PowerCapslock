# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PowerCapslock is a Windows keyboard hotkey mapping tool that transforms CapsLock into a modifier key for Vim-style navigation and function key mappings. It uses Windows Low-level Keyboard Hook (WH_KEYBOARD_LL) to intercept and remap keys.

## Build Commands

```batch
# Build with CMake (auto-detects MinGW or MSVC)
scripts\build.bat

# Manual build (MinGW)
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make -j4

# Manual build (MSVC)
mkdir build && cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
nmake
```

## Installation

```batch
# Install (requires admin privileges)
scripts\install.bat

# Uninstall (requires admin privileges)
scripts\uninstall.bat
```

## Architecture

### Core Components

| Component | File | Description |
|-----------|------|-------------|
| **Hook** | [hook.c](src/hook.c)/[h](src/hook.h) | Low-level keyboard hook installation and callback. Intercepts CapsLock and mapped keys, uses `SendInput()` to simulate target keys. |
| **Keymap** | [keymap.c](src/keymap.c)/[h](src/keymap.h) | Key mapping table management. Uses Scan Codes (not VK codes) for multi-language keyboard support. |
| **Tray** | [tray.c](src/tray.c)/[h](src/tray.h) | System tray icon and menu for enable/disable control. |
| **Config** | [config.c](src/config.c)/[h](src/config.h) | JSON configuration file loading/parsing. Config location: `%APPDATA%\PowerCapslock\config.json`. |
| **Logger** | [logger.c](src/logger.c)/[h](src/logger.h) | File/console logging with DEBUG/INFO/WARN/ERROR levels. Log location: `%USERPROFILE%\.PowerCapslock\logs\powercapslock.log`. |
| **Keyboard Layout** | [keyboard_layout.c](src/keyboard_layout.c)/[h](src/keyboard_layout.h) | Multi-language keyboard layout support via `MapVirtualKeyEx()`. |
| **Voice Input** | [voice_input.c](src/voice_input.c)/[h](src/voice_input.h) | Voice-to-text input functionality using VolcEngine ARK API. |
| **Screenshot** | [screenshot.c](src/screenshot.c)/[h](src/screenshot.h) | Screen capture functionality with region selection, annotation, and OCR. |
| **Screenshot Manager** | [screenshot_manager.c](src/screenshot_manager.c)/[h](src/screenshot_manager.h) | Manages the complete screenshot workflow and UI components. |

### Screenshot Feature Modules

| Module | File | Description |
|--------|------|-------------|
| **Core Capture** | [screenshot.c](src/screenshot.c)/[h](src/screenshot.h) | Basic screen capture (full screen, region, monitor). Supports BMP/PNG/JPEG save, clipboard copy. |
| **Selection Overlay** | [screenshot_overlay.c](src/screenshot_overlay.c)/[h](src/screenshot_overlay.h) | Transparent overlay window for selecting screen region with mouse drag. |
| **Toolbar** | [screenshot_toolbar.c](src/screenshot_toolbar.c)/[h](src/screenshot_toolbar.h) | Floating toolbar with save/copy/cancel buttons for screenshot workflow. |
| **Float Window** | [screenshot_float.c](src/screenshot_float.c)/[h](src/screenshot_float.h) | Floating preview window for captured screenshot. |
| **Annotation** | [screenshot_annotate.c](src/screenshot_annotate.c)/[h](src/screenshot_annotate.h) | Drawing tools (rectangle, arrow, pencil, circle, text) for annotating screenshots. |
| **OCR** | [screenshot_ocr.c](src/screenshot_ocr.c)/[h](src/screenshot_ocr.h) | Text recognition from screenshots using Tesseract OCR engine. |

### Key Technical Details

- **Scan Code based mapping**: Uses hardware scan codes (from `KBDLLHOOKSTRUCT.scanCode`) instead of virtual key codes. This ensures mappings work regardless of keyboard language/layout.
- **Hook callback flow**: `LowLevelKeyboardProc` checks CapsLock state → sets `capslockHeld` flag → looks up scan code in mapping table → sends target key via `SendInput()`.
- **Key interception**: Return 1 from hook callback to block original key; call `CallNextHookEx()` to pass through.

### Default Key Mappings

| CapsLock + Key | Target |
|----------------|--------|
| H/J/K/L | Arrow keys |
| I/O | Home/End |
| U/P | PageDown/PageUp |
| 1-9, 0, -, = | F1-F12 |
| X | Screenshot (built-in function) |

## Configuration

JSON config at `%APPDATA%\PowerCapslock\config.json`. See [config/config.json](config/config.json) for default structure.

## Development Workflow

### Testing Requirements

1. **Self-verification**: Every new feature or bug fix must be verified using:
   - Python test scripts
   - Program log analysis

2. **Test artifacts location**: All test files must be placed in `test/` directory
   - Do not pollute the project root or source directories

3. **Log location**: Program logs are stored at:
   - Path: `%USERPROFILE%\.PowerCapslock\logs\`
   - Main log: `powercapslock.log`

4. **Singleton enforcement**: The program enforces single-instance mode
   - **Always kill existing process before testing**: `taskkill /F /IM PowerCapslock.exe`

### Debug Commands

```batch
# View logs in real-time
tail -f %USERPROFILE%\.PowerCapslock\logs\powercapslock.log

# Kill existing process before testing
taskkill /F /IM PowerCapslock.exe

# Run from console for direct output
PowerCapslock.exe --console
```

### Screenshot Testing Commands

```batch
# Test screenshot capture
PowerCapslock.exe --test-screenshot-capture 0 0 200 200 capture.bmp

# Test screenshot save
PowerCapslock.exe --test-screenshot-save save.bmp

# Test clipboard copy
PowerCapslock.exe --test-screenshot-clipboard

# Run all screenshot tests
PowerCapslock.exe --test-screenshot-all

# Test individual UI components
PowerCapslock.exe --test-screenshot-overlay
PowerCapslock.exe --test-screenshot-toolbar
PowerCapslock.exe --test-screenshot-float
PowerCapslock.exe --test-screenshot-annotate
PowerCapslock.exe --test-screenshot-ocr
```

### Python Test Suite

```batch
# Run the full screenshot functional test suite
cd test
python screenshot_function_test.py --stop-existing
```

## Troubleshooting

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| Keys not responding | Singleton conflict | Use Task Manager to end `PowerCapslock.exe` and retry |
| Chinese characters garbled | Encoding issue | Ensure `config.json` is saved in UTF-8 encoding |
| Logs not written | Permission issue | Verify `%USERPROFILE%` directory is writable |
| Build fails | CMake generator mismatch | Delete `build/` directory and retry with correct generator |
| Tray icon not showing | Explorer shell issue | Restart explorer.exe or re-login Windows |
| Screenshot not triggering | Modules not initialized | **Fixed in v1.0.1**: Ensure `ScreenshotManagerInit()` initializes all required modules (overlay, toolbar, float, OCR) |
| Screenshot shortcut not working | Config not loaded | Check that config has `{"trigger": "X", "type": "builtin", "param": "screenshot"}` and restart the program |

### Screenshot Debug Checklist

1. **Verify config**: Check `%USERPROFILE%\.PowerCapslock\config\config.json` has the screenshot action
2. **Check logs**: Look for `[截图管理器]` entries in `%USERPROFILE%\.PowerCapslock\logs\powercapslock.log`
3. **Run tests**: Use `--test-screenshot-all` to verify all screenshot components work
4. **Restart program**: Always restart after configuration changes

## Recent Fixes

### 2026-04-18: Screenshot Manager Initialization Bug

**Issue**: CapsLock+X screenshot shortcut was not triggering the screenshot UI.

**Root Cause**: `ScreenshotManagerInit()` in `src/screenshot_manager.c` was only initializing the core `ScreenshotInit()` module, but not the dependent UI modules:
- `ScreenshotOverlayInit()` - Selection overlay window
- `ScreenshotToolbarInit()` - Toolbar UI
- `ScreenshotFloatInit()` - Float preview window
- `OCRInit()` - OCR module

When `ScreenshotManagerStart()` tried to call `ScreenshotOverlayShow()`, it would fail because the overlay module was not initialized.

**Fix**: Updated `ScreenshotManagerInit()` and `ScreenshotManagerCleanup()` to properly initialize and cleanup all required modules.

**Files Modified**:
- `src/screenshot_manager.c`

**Verification**:
- All 10 screenshot tests pass (`--test-screenshot-all`)
- CapsLock+X now correctly brings up the selection overlay

## Important Files

- [CMakeLists.txt](CMakeLists.txt) - Build configuration
- [src/main.c](src/main.c) - Entry point and module initialization
- [src/hook.c](src/hook.c) - Core keyboard interception logic
- [DESIGN.md](DESIGN.md) - Detailed technical design document (in Chinese)

## Release Packaging Requirements

### Critical: Must Include Resources Directory

**When creating release packages, always ensure the following file is included:**

```
resources/config_ui.html
```

This file is required for the configuration dialog to work properly. Without it, users cannot:
- Edit key mappings
- Configure built-in functions (like screenshot)
- Manage custom actions

### Required Files for Release Zip

A complete release package must include:

```
PowerCapslock-vX.Y.Z.zip/
├── powercapslock.exe
├── onnxruntime.dll
├── onnxruntime_providers_shared.dll
├── sherpa-onnx-c-api.dll
├── sherpa-onnx-cxx-api.dll
├── WebView2Loader.dll
├── README.txt
├── RELEASE_NOTES.md
└── resources/
    └── config_ui.html  ← NEVER FORGET THIS!
```

### Verification Checklist Before Release

- [ ] `resources/config_ui.html` exists in the build output directory
- [ ] Zip package includes `resources/config_ui.html`
- [ ] All required DLLs are included
- [ ] README.txt and RELEASE_NOTES.md are up to date
- [ ] Run `--test-config` to verify config dialog works
- [ ] Run all screenshot tests to verify functionality

### CMake Post-Build Configuration

The CMakeLists.txt is already configured to copy `resources/config_ui.html` to the build directory automatically:

```cmake
add_custom_command(TARGET powercapslock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:powercapslock>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/config_ui.html
        $<TARGET_FILE_DIR:powercapslock>/resources/config_ui.html
    # ... other DLL copies
)
```

Always build first, then package from the build output directory.
