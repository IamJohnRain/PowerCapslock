# PowerCapslock

Windows 键盘热键映射工具 - 将 CapsLock 改造为强大的修饰键

## 功能特性

- **方向键映射**: CapsLock + h/j/k/l → 左/下/上/右
- **功能键映射**:
  - CapsLock + i/o → Home/End
  - CapsLock + u/p → PageDown/PageUp
  - CapsLock + 1-9,0,-,= → F1-F12
- **本地语音输入**: CapsLock + A 触发语音转文字（基于 sherpa-onnx 本地引擎）
- **多语言支持**: 基于物理按键位置，不受键盘布局影响
- **系统托盘**: 可视化控制，一键启用/禁用
- **日志调试**: 详细的运行日志，便于排查问题
- **开机启动**: 支持 Windows 开机自动运行
- **可视化配置**: Web 配置页面，支持自定义键位映射和模型路径

## 快速开始

### 安装

1. 下载最新版本 [GitHub Releases](https://github.com/IamJohnRain/PowerCapslock/releases)
2. 解压到任意目录
3. 右键 `install.bat`，选择"以管理员身份运行"
4. 程序将自动完成安装并添加到开机启动项

### 使用

- 安装后，CapsLock 键将作为修饰键
- 按住 CapsLock + 映射键即可触发对应功能
- 点击系统托盘图标可启用/禁用程序
- 右键托盘图标 → 打开配置页面，可进行可视化配置

### 语音输入配置

1. 下载 sherpa-onnx SenseVoice 模型：
   - 访问 [sherpa-onnx releases](https://github.com/k2-fsa/sherpa-onnx/releases)
   - 下载 `sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2`
   - 解压到任意目录

2. 在配置页面的"语音设置"中，设置模型路径

3. 使用 CapsLock + A 触发语音输入

## 项目结构

```
PowerCapslock/
├── src/                  # 源代码
│   ├── main.c           # 程序入口
│   ├── hook.c/h         # 键盘钩子核心
│   ├── keymap.c/h       # 键位映射管理
│   ├── tray.c/h         # 系统托盘
│   ├── config.c/h       # 配置文件
│   ├── logger.c/h       # 日志系统
│   ├── voice.c/h        # 语音识别
│   ├── audio.c/h        # 音频采集
│   ├── voice_prompt.c/h # 语音提示窗口
│   ├── config_dialog_webview.cpp # Web配置页面
│   └── ...
├── resources/            # 资源文件
├── test/                 # 测试脚本
│   ├── config_function_test.py    # 配置功能测试
│   ├── powercapslock_function_test.py  # 功能测试
│   ├── webview_config_test.py     # Web配置测试
│   └── test_capslock_a_bug.py     # CapsLock+A测试
├── scripts/              # 构建和安装脚本
│   ├── build.bat        # 构建脚本
│   ├── install.bat      # 安装脚本
│   └── uninstall.bat    # 卸载脚本
├── lib/                  # 第三方库
│   └── sherpa-onnx/     # sherpa-onnx 库
├── build/                # 构建输出目录
├── CMakeLists.txt       # CMake 配置
├── README.md            # 项目说明
├── CLAUDE.md            # Claude Code 指引
└── index.html           # 项目主页
```

## 构建指南

### 环境要求

- Windows 10/11
- MinGW-w64 (推荐) 或 MSVC
- CMake 3.10+
- Python 3.8+ (用于测试)

### 构建步骤

**方式一：使用 build.bat（推荐）**

```batch
cd PowerCapslock
scripts\build.bat
```

**方式二：手动构建**

```batch
cd PowerCapslock

# 创建构建目录
mkdir build
cd build

# 生成构建配置（MinGW）
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 编译
mingw32-make -j4

# 或者使用 MSVC
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
nmake
```

### 安装

```batch
# 以管理员身份运行安装脚本
scripts\install.bat
```

安装脚本会：
1. 复制可执行文件到 `%ProgramFiles%\PowerCapslock`
2. 创建配置文件目录 `%APPDATA%\PowerCapslock`
3. 添加到系统开机启动项
4. 启动程序

## 测试

### 运行功能测试

```batch
# 确保程序未运行
taskkill /F /IM PowerCapslock.exe 2>nul

# 运行配置功能测试
python test/config_function_test.py --stop-existing

# 运行完整功能测试
python test/powercapslock_function_test.py --stop-existing

# 运行Web配置测试
python test/webview_config_test.py --stop-existing
```

### 测试覆盖

- **配置功能测试**: 验证配置文件的读写、键位映射、日志设置等
- **功能测试**: 验证键盘钩子、键位映射、系统托盘等功能
- **Web配置测试**: 验证可视化配置页面的各项功能
- **语音功能测试**: 验证语音输入、音频采集、模型加载等

## 技术架构

- **核心技术**: Windows Low-level Keyboard Hook (WH_KEYBOARD_LL)
- **开发语言**: C11 / C++17
- **构建工具**: CMake
- **键位识别**: Scan Code（支持多语言布局）
- **语音识别**: sherpa-onnx + SenseVoice 本地模型
- **配置界面**: WebView2 (Microsoft Edge WebView2)

## 许可证

MIT License

## 致谢

- 感谢所有贡献者和测试者
- 灵感来源于 Vim 编辑器的键位设计
- 语音识别基于 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) 项目
