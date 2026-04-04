# JohnHotKeyMap

Windows 键盘热键映射工具 - 将 CapsLock 改造为强大的修饰键

## 功能特性

- **方向键映射**: CapsLock + h/j/k/l → 左/下/上/右
- **功能键映射**: 
  - CapsLock + i/o → Home/End
  - CapsLock + u/p → PageDown/PageUp
  - CapsLock + 1-9,0,-,= → F1-F12
- **多语言支持**: 基于物理按键位置，不受键盘布局影响
- **系统托盘**: 可视化控制，一键启用/禁用
- **日志调试**: 详细的运行日志，便于排查问题
- **开机启动**: 支持 Windows 开机自动运行
- **配置灵活**: JSON 配置文件，支持自定义映射

## 快速开始

### 安装

1. 下载最新版本
2. 运行 `install.bat`（需要管理员权限）
3. 程序将自动启动并添加到开机启动项

### 使用

- 安装后，CapsLock 键将作为修饰键
- 按住 CapsLock + 映射键即可触发对应功能
- 点击系统托盘图标可启用/禁用程序
- 右键托盘图标打开菜单

### 配置文件

配置文件位置：`%APPDATA%\JohnHotKeyMap\keymap.json`

```json
{
    "modifier": {
        "key": "CAPSLOCK",
        "suppress_original": true
    },
    "mappings": [
        {"from": "H", "to": "LEFT"},
        {"from": "J", "to": "DOWN"},
        {"from": "K", "to": "UP"},
        {"from": "L", "to": "RIGHT"}
    ],
    "options": {
        "start_enabled": true,
        "log_level": "INFO"
    }
}
```

## 构建

### 环境要求

- Windows 7/8/10/11
- MinGW-w64 或 MSVC
- CMake 3.10+

### 构建步骤

```batch
# 克隆仓库
git clone https://github.com/yourusername/JohnHotKeyMap.git
cd JohnHotKeyMap

# 构建
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j4

# 安装
..\scripts\install.bat
```

## 技术架构

- **核心技术**: Windows Low-level Keyboard Hook (WH_KEYBOARD_LL)
- **开发语言**: C11
- **构建工具**: CMake
- **键位识别**: Scan Code（支持多语言布局）

## 项目结构

```
JohnHotKeyMap/
├── src/              # 源代码
├── resources/        # 资源文件
├── config/           # 默认配置
├── scripts/          # 构建和安装脚本
├── DESIGN.md         # 技术设计文档
└── README.md         # 本文件
```

## 许可证

MIT License

## 致谢

- 感谢所有贡献者和测试者
- 灵感来源于 Vim 编辑器的键位设计
