#pragma once
#include <stdint.h>

void hidBeginUsb();
bool hidUsbHostSeen();
void hidStartBle(const char* name, int slot);
void hidUseUsb(bool usb);
bool hidLinked();
void hidMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
void hidKeyboard(uint8_t mods, const uint8_t keys[6]);
void hidRelease();
bool hidTakeAuthEvent();
void hidSyncBonds(int slot);
void hidClearSlot(int slot);
