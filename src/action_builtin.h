#ifndef ACTION_BUILTIN_H
#define ACTION_BUILTIN_H

#include <stdbool.h>

// 内置功能处理器类型
typedef bool (*BuiltinHandler)(void);

// 初始化内置功能模块
void BuiltinInit(void);

// 清理内置功能模块
void BuiltinCleanup(void);

// 注册内置功能
void BuiltinRegister(const char* name, BuiltinHandler handler);

// 执行内置功能
bool BuiltinExecute(const char* name);

// 获取所有已注册的内置功能名称
// 返回: 名称数组，count 输出数量
const char** BuiltinGetList(int* count);

// 获取内置功能的显示名称（中文）
const char* BuiltinGetDisplayName(const char* name);

#endif // ACTION_BUILTIN_H
