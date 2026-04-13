#ifndef SCREENSHOT_MANAGER_H
#define SCREENSHOT_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ScreenshotManagerInit(void);
void ScreenshotManagerCleanup(void);
bool ScreenshotManagerStart(void);
bool ScreenshotManagerIsActive(void);

#ifdef __cplusplus
}
#endif

#endif
