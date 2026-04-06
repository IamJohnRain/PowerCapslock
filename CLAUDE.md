# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

JohnHotKeyMap is a Windows keyboard hotkey mapping tool that transforms CapsLock into a modifier key for Vim-style navigation and function key mappings. It uses Windows Low-level Keyboard Hook (WH_KEYBOARD_LL) to intercept and remap keys.

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

- **hook.c/h**: Low-level keyboard hook installation and callback. Intercepts CapsLock and mapped keys, uses `SendInput()` to simulate target keys.
- **keymap.c/h**: Key mapping table management. Uses Scan Codes (not VK codes) for multi-language keyboard support.
- **tray.c/h**: System tray icon and menu for enable/disable control.
- **config.c/h**: JSON configuration file loading/parsing. Config location: `%APPDATA%\JohnHotKeyMap\keymap.json`.
- **logger.c/h**: File/console logging with DEBUG/INFO/WARN/ERROR levels. Log location: `%APPDATA%\JohnHotKeyMap\logs\hotkeymap.log`.
- **keyboard_layout.c/h**: Multi-language keyboard layout support via `MapVirtualKeyEx()`.

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

JSON config at `%APPDATA%\JohnHotKeyMap\keymap.json`. See [config/keymap.json](config/keymap.json) for default structure.

## Important Files

- [CMakeLists.txt](CMakeLists.txt) - Build configuration
- [src/main.c](src/main.c) - Entry point and module initialization
- [src/hook.c](src/hook.c) - Core keyboard interception logic
- [DESIGN.md](DESIGN.md) - Detailed technical design document (in Chinese)
