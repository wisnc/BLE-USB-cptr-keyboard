#pragma once
#include <stdint.h>

enum : uint8_t {
    MB_L  = 0x01,
    MB_R  = 0x02,
    MB_M  = 0x04,
    MB_4  = 0x08,
    MB_5  = 0x10,
    MB_WU = 0x20,
    MB_WD = 0x40
};

void uiBegin(uint32_t themeRgb);
void uiKeyboardScreen();
void uiMouseScreen();
void uiStatus(bool mouseMode, bool usb, int slot, bool linked);
void uiKeys(const uint16_t rows[4]);
void uiGauge(bool on, float fx, float fy);
void uiMouse(uint8_t bits);
void uiMessage(const char* a, const char* b);
