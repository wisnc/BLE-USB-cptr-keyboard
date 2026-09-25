#pragma once
#include <M5GFX.h>

struct Surf {
    LovyanGFX* g;
    int ox;
    int oy;
};

void gPixel(const Surf& s, int x, int y, uint16_t c);
void gHLine(const Surf& s, int x, int y, int w, uint16_t c);
void gVLine(const Surf& s, int x, int y, int h, uint16_t c);
void gFillRect(const Surf& s, int x, int y, int w, int h, uint16_t c);
void gDrawRect(const Surf& s, int x, int y, int w, int h, uint16_t c);
void gCircle(const Surf& s, int x0, int y0, int r, uint16_t c);
void gFillCircle(const Surf& s, int x0, int y0, int r, uint16_t c);
void gRoundRect(const Surf& s, int x, int y, int w, int h, int r, uint16_t c);
void gFillRoundRect(const Surf& s, int x, int y, int w, int h, int r, uint16_t c);
void gClip(const Surf& s, int x, int y, int w, int h);
void gUnclip(const Surf& s);
void gText0(const Surf& s, int x, int y, const char* t, uint16_t c);
int gW0(const char* t);
void gTextTT(const Surf& s, int x, int y, const char* t, uint16_t c);
int gWTT(const char* t);
