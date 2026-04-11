#ifndef CONFIG_DIALOG_H
#define CONFIG_DIALOG_H

#include <stdbool.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// 显示配置对话框，父窗口句柄
// 返回 true 表示配置已修改并保存，false 表示取消或无修改
bool ShowConfigDialog(HWND hParent);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_DIALOG_H
