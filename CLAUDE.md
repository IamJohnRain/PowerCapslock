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

## Troubleshooting

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| Keys not responding | Singleton conflict | Use Task Manager to end `PowerCapslock.exe` and retry |
| Chinese characters garbled | Encoding issue | Ensure `config.json` is saved in UTF-8 encoding |
| Logs not written | Permission issue | Verify `%USERPROFILE%` directory is writable |
| Build fails | CMake generator mismatch | Delete `build/` directory and retry with correct generator |
| Tray icon not showing | Explorer shell issue | Restart explorer.exe or re-login Windows |

## Important Files

- [CMakeLists.txt](CMakeLists.txt) - Build configuration
- [src/main.c](src/main.c) - Entry point and module initialization
- [src/hook.c](src/hook.c) - Core keyboard interception logic
- [DESIGN.md](DESIGN.md) - Detailed technical design document (in Chinese)
