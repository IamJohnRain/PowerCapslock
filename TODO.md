# JohnHotKeyMap 开发任务清单

## 已完成

- [x] 技术方案设计文档 (DESIGN.md)

## 待开发

### 核心模块

#### 1. 键位映射模块 (keymap.c/h)
- [ ] keymap.h - 头文件定义
- [ ] keymap.c - 实现键位映射表管理
  - [ ] 默认映射表初始化
  - [ ] 基于 Scan Code 的查找
  - [ ] 动态添加/删除映射
  - [ ] 重置为默认

#### 2. 日志模块 (logger.c/h)
- [ ] logger.h - 头文件定义
- [ ] logger.c - 实现日志系统
  - [ ] 初始化/清理
  - [ ] 分级日志 (DEBUG/INFO/WARN/ERROR)
  - [ ] 文件日志输出
  - [ ] 控制台日志输出（调试模式）

#### 3. 配置模块 (config.c/h)
- [ ] config.h - 头文件定义
- [ ] config.c - 实现配置管理
  - [ ] JSON 配置文件解析
  - [ ] 默认配置生成
  - [ ] 配置保存
  - [ ] 路径管理（APPDATA）

#### 4. 键盘钩子模块 (hook.c/h)
- [ ] hook.h - 头文件定义
- [ ] hook.c - 实现键盘钩子
  - [ ] 安装/卸载钩子
  - [ ] 回调函数处理
  - [ ] CapsLock 检测
  - [ ] 键位映射触发
  - [ ] SendInput 模拟按键

#### 5. 系统托盘模块 (tray.c/h)
- [ ] tray.h - 头文件定义
- [ ] tray.c - 实现系统托盘
  - [ ] 托盘图标管理
  - [ ] 右键菜单
  - [ ] 状态切换（启用/禁用）
  - [ ] 消息处理

#### 6. 键盘布局模块 (keyboard_layout.c/h)
- [ ] keyboard_layout.h - 头文件定义
- [ ] keyboard_layout.c - 实现布局支持
  - [ ] 获取当前布局
  - [ ] VK <-> Scan Code 转换
  - [ ] 布局名称获取

#### 7. 主程序 (main.c)
- [ ] main.c - 程序入口
  - [ ] 初始化所有模块
  - [ ] 消息循环
  - [ ] 清理资源

### 资源文件

- [ ] 托盘图标 (icon.ico)
- [ ] 禁用状态图标 (icon_disabled.ico)
- [ ] Windows 资源文件 (resource.rc)

### 配置文件

- [ ] 默认配置文件 (config/keymap.json)

### 构建系统

- [ ] CMakeLists.txt
- [ ] 构建脚本 (scripts/build.bat)
- [ ] 安装脚本 (scripts/install.bat)
- [ ] 卸载脚本 (scripts/uninstall.bat)

### 文档

- [ ] README.md - 使用说明
- [ ] CHANGELOG.md - 更新日志

## 开发顺序建议

1. **第一阶段 - 核心功能**
   - keymap (键位映射表)
   - logger (日志，便于调试)
   - hook (键盘钩子核心)
   - main (基本框架)

2. **第二阶段 - 功能完善**
   - config (配置管理)
   - keyboard_layout (多语言支持)
   - tray (系统托盘)

3. **第三阶段 - 打包发布**
   - 资源文件
   - 构建脚本
   - 安装程序
   - 文档完善

## 技术要点备忘

### Scan Code 参考值
```
H: 0x23, J: 0x24, K: 0x25, L: 0x26
I: 0x17, O: 0x18, U: 0x16, P: 0x15
1: 0x02, 2: 0x03, 3: 0x04, 4: 0x05
5: 0x06, 6: 0x07, 7: 0x08, 8: 0x09
9: 0x0A, 0: 0x0B, -: 0x0C, =: 0x0D
```

### 目标 VK Code
```
VK_LEFT, VK_DOWN, VK_UP, VK_RIGHT
VK_HOME, VK_END, VK_NEXT(PgDn), VK_PRIOR(PgUp)
VK_F1 - VK_F12
```

### 关键 Windows API
```c
SetWindowsHookEx(WH_KEYBOARD_LL, ...)
CallNextHookEx(...)
SendInput(...)
MapVirtualKeyEx(...)
Shell_NotifyIcon(...)
```

### 文件路径
```
程序目录: C:\Program Files\JohnHotKeyMap\
配置目录: %APPDATA%\JohnHotKeyMap\
日志目录: %APPDATA%\JohnHotKeyMap\logs\
```

## 测试清单

- [ ] 基本映射功能测试
- [ ] 多语言键盘布局测试
- [ ] 系统托盘功能测试
- [ ] 开机启动测试
- [ ] 配置文件加载/保存测试
- [ ] 日志功能测试
- [ ] 长时间运行稳定性测试
- [ ] 与其他软件兼容性测试
