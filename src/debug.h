#pragma once
#include <stdint.h>

struct DebugState {
    int slot;
    int gen;
    char name[40];
    char addr[20];
    int nvsBoots;
    int sdBoots;
    int loadState;
    int saveState;
    int bondsBoot;
    volatile bool connected;
    volatile bool encrypted;
    char peer[20];
};

extern DebugState dbg;

void dbgBootCounters();
void dbgLog(bool alert, const char* fmt, ...);
void dbgAddr(char* out, const uint8_t* a);
void dbgScreen();
void dbgUpdate(bool mouseMode, bool usb, uint32_t now);
