# JohnHotKeyMap 构建和测试指南

## 项目状态

✅ 所有源代码已完成编写
✅ 配置文件已创建
✅ 构建脚本已准备就绪
⚠️ 需要安装编译器才能构建

## 项目结构

```
JohnHotKeyMap2/
├── src/                    # 源代码
│   ├── main.c             # 主程序入口
│   ├── hook.c/h           # 键盘钩子模块
│   ├── keymap.c/h         # 键位映射模块
│   ├── tray.c/h           # 系统托盘模块
│   ├── config.c/h         # 配置管理模块
│   ├── logger.c/h         # 日志系统模块
│   └── keyboard_layout.c/h # 键盘布局模块
├── resources/              # 资源文件
│   ├── icon.ico           # 正常状态图标
│   ├── icon_disabled.ico  # 禁用状态图标
│   └── resource.rc        # Windows资源文件
├── config/                 # 默认配置
│   └── keymap.json        # 默认配置文件
├── scripts/                # 构建脚本
│   ├── build.bat          # 构建脚本
│   ├── install.bat        # 安装脚本
│   ├── uninstall.bat      # 卸载脚本
│   └── build_simple.bat   # 构建说明脚本
├── CMakeLists.txt         # CMake配置
├── README.md              # 项目说明
├── DESIGN.md              # 技术设计文档
└── TODO.md                # 开发任务清单
```

## 快速开始

### 1. 安装编译环境

#### 选项A: MinGW-w64 (推荐)

1. 下载 MinGW-w64:
   - 官网: https://www.mingw-w64.org/
   - 推荐: https://github.com/niXman/mingw-builds-binaries/releases
   - 下载 `x86_64-posix-seh` 版本

2. 解压到 `C:\mingw64`

3. 添加到系统PATH:
   ```
   C:\mingw64\bin
   ```

4. 下载并安装 CMake:
   - https://cmake.org/download/
   - 添加到PATH

#### 选项B: Microsoft Visual Studio

1. 安装 Visual Studio 2019 或更高版本
2. 选择 "Desktop development with C++" 工作负载
3. 使用 "Developer Command Prompt for VS"

### 2. 构建项目

#### 使用构建脚本 (推荐)

```batch
scripts\build.bat
```

#### 手动构建 (MinGW)

```batch
# 创建构建目录
mkdir build
cd build

# 配置
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 编译
mingw32-make -j4
```

#### 手动构建 (MSVC)

```batch
# 创建构建目录
mkdir build
cd build

# 配置
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release

# 编译
nmake
```

### 3. 安装程序

```batch
# 需要管理员权限
scripts\install.bat
```

安装脚本会:
- 复制程序到 `C:\Program Files\JohnHotKeyMap\`
- 创建配置目录 `%APPDATA%\JohnHotKeyMap\`
- 添加到开机启动
- 创建开始菜单快捷方式

### 4. 测试功能

#### 基本测试

1. 运行程序:
   ```
   "C:\Program Files\JohnHotKeyMap\hotkeymap.exe"
   ```

2. 检查系统托盘:
   - 应该看到绿色图标
   - 右键点击查看菜单

3. 测试键位映射:
   - 按住 `CapsLock`
   - 按 `H` → 应该触发左方向键
   - 按 `J` → 应该触发下方向键
   - 按 `K` → 应该触发上方向键
   - 按 `L` → 应该触发右方向键

4. 测试功能键:
   - `CapsLock + 1` → F1
   - `CapsLock + 2` → F2
   - ... 以此类推

#### 日志检查

查看日志文件:
```
%APPDATA%\JohnHotKeyMap\logs\hotkeymap.log
```

#### 配置测试

编辑配置文件:
```
%APPDATA%\JohnHotKeyMap\keymap.json
```

### 5. 卸载程序

```batch
# 需要管理员权限
scripts\uninstall.bat
```

## 功能测试清单

- [ ] 程序启动正常
- [ ] 系统托盘图标显示
- [ ] 左键点击托盘图标切换状态
- [ ] 右键点击托盘图标显示菜单
- [ ] CapsLock + H/J/K/L 方向键映射
- [ ] CapsLock + I/O Home/End映射
- [ ] CapsLock + U/P PageDown/PageUp映射
- [ ] CapsLock + 1-9,0,-,= F1-F12映射
- [ ] 日志文件正常记录
- [ ] 配置文件加载正常
- [ ] 开机自动启动
- [ ] 多语言键盘布局支持
- [ ] 长时间运行稳定

## 已知问题

1. **杀毒软件**: 可能被误报为键盘记录器
   - 解决: 添加到杀毒软件白名单

2. **游戏兼容性**: 部分游戏可能不生效
   - 原因: 游戏使用 DirectInput 或 Raw Input
   - 解决: 文档说明限制

3. **权限问题**: 安装需要管理员权限
   - 原因: 需要写入 Program Files 和注册表

## 开发说明

### 代码架构

- **模块化设计**: 每个功能独立模块
- **线程安全**: 使用临界区保护共享资源
- **错误处理**: 完善的错误检查和日志
- **资源管理**: 正确的初始化和清理

### 关键技术

- **Windows Low-level Keyboard Hook**: 全局键盘拦截
- **Scan Code**: 物理按键识别,支持多语言
- **SendInput**: 模拟键盘输入
- **System Tray**: 系统托盘界面

### 扩展建议

1. 添加更多键位映射
2. 支持多个修饰键
3. 添加配置GUI界面
4. 支持配置热重载
5. 添加宏功能

## 许可证

MIT License

## 联系方式

- 作者: John
- 项目: JohnHotKeyMap
- 版本: 1.0.0
