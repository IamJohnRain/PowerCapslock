#include "keymap.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

// 动态映射表
typedef struct {
    KeyMapping* mappings;   // 映射数组
    int count;              // 当前数量
    int capacity;           // 容量
} KeymapTable;

static KeymapTable g_keymap = {0};

// 默认映射表（基于美式键盘物理布局的 Scan Code）
static const KeyMapping defaultMappings[] = {
    // 方向键
    {0x23, VK_LEFT,  "H->Left"},    // H
    {0x24, VK_DOWN,  "J->Down"},    // J
    {0x25, VK_UP,    "K->Up"},      // K
    {0x26, VK_RIGHT, "L->Right"},   // L

    // 功能键
    {0x17, VK_HOME,  "I->Home"},    // I
    {0x18, VK_END,   "O->End"},     // O
    {0x16, VK_NEXT,  "U->PageDown"},// U (PageDown)
    {0x19, VK_PRIOR, "P->PageUp"},  // P (PageUp)

    // F键 (1-9,0,-,=)
    {0x02, VK_F1,  "1->F1"},   // 1
    {0x03, VK_F2,  "2->F2"},   // 2
    {0x04, VK_F3,  "3->F3"},   // 3
    {0x05, VK_F4,  "4->F4"},   // 4
    {0x06, VK_F5,  "5->F5"},   // 5
    {0x07, VK_F6,  "6->F6"},   // 6
    {0x08, VK_F7,  "7->F7"},   // 7
    {0x09, VK_F8,  "8->F8"},   // 8
    {0x0A, VK_F9,  "9->F9"},   // 9
    {0x0B, VK_F10, "0->F10"},  // 0
    {0x0C, VK_F11, "->F11"},   // -
    {0x0D, VK_F12, "=>F12"}    // =
};

static const int defaultMappingCount = sizeof(defaultMappings) / sizeof(defaultMappings[0]);

void KeymapInit(void) {
    // 初始化动态数组
    g_keymap.capacity = 32;
    g_keymap.mappings = (KeyMapping*)malloc(g_keymap.capacity * sizeof(KeyMapping));
    if (g_keymap.mappings == NULL) {
        LOG_ERROR("Failed to allocate memory for keymap");
        return;
    }
    g_keymap.count = 0;

    // 加载默认映射
    KeymapResetToDefaults();

    LOG_INFO("Keymap initialized with %d mappings", g_keymap.count);
}

void KeymapCleanup(void) {
    if (g_keymap.mappings != NULL) {
        free(g_keymap.mappings);
        g_keymap.mappings = NULL;
    }
    g_keymap.count = 0;
    g_keymap.capacity = 0;
}

const KeyMapping* KeymapFindByScanCode(WORD scanCode) {
    for (int i = 0; i < g_keymap.count; i++) {
        if (g_keymap.mappings[i].scanCode == scanCode) {
            return &g_keymap.mappings[i];
        }
    }
    return NULL;
}

void KeymapAddMapping(WORD scanCode, UINT targetVk, const char* name) {
    // 检查是否已存在
    for (int i = 0; i < g_keymap.count; i++) {
        if (g_keymap.mappings[i].scanCode == scanCode) {
            // 更新现有映射
            g_keymap.mappings[i].targetVk = targetVk;
            g_keymap.mappings[i].name = name;
            LOG_DEBUG("Updated mapping: %s", name);
            return;
        }
    }

    // 检查容量
    if (g_keymap.count >= g_keymap.capacity) {
        int newCapacity = g_keymap.capacity;
        if (newCapacity == 0) {
            newCapacity = 32; // Initial capacity if not initialized
        } else {
            newCapacity = g_keymap.capacity * 2;
        }
        KeyMapping* newMappings = (KeyMapping*)realloc(g_keymap.mappings,
                                                        newCapacity * sizeof(KeyMapping));
        if (newMappings == NULL) {
            LOG_ERROR("Failed to expand keymap capacity");
            return;
        }
        g_keymap.mappings = newMappings;
        g_keymap.capacity = newCapacity;
    }

    // 添加新映射
    g_keymap.mappings[g_keymap.count].scanCode = scanCode;
    g_keymap.mappings[g_keymap.count].targetVk = targetVk;
    g_keymap.mappings[g_keymap.count].name = name;
    g_keymap.count++;

    LOG_DEBUG("Added mapping: %s", name);
}

void KeymapClear(void) {
    g_keymap.count = 0;
    LOG_DEBUG("Keymap cleared");
}

void KeymapResetToDefaults(void) {
    KeymapClear();

    for (int i = 0; i < defaultMappingCount; i++) {
        KeymapAddMapping(defaultMappings[i].scanCode,
                        defaultMappings[i].targetVk,
                        defaultMappings[i].name);
    }

    LOG_INFO("Keymap reset to defaults (%d mappings)", g_keymap.count);
}

int KeymapGetCount(void) {
    return g_keymap.count;
}

const KeyMapping* KeymapGetAll(int* count) {
    if (count != NULL) {
        *count = g_keymap.count;
    }
    return g_keymap.mappings;
}
