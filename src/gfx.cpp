#include "gfx.h"
#include "tomthumb.h"
#include <string.h>

void gPixel(const Surf& s, int x, int y, uint16_t c) {
    s.g->drawPixel(x - s.ox, y - s.oy, c);
}

void gHLine(const Surf& s, int x, int y, int w, uint16_t c) {
    if (w > 0) s.g->drawFastHLine(x - s.ox, y - s.oy, w, c);
}

void gVLine(const Surf& s, int x, int y, int h, uint16_t c) {
    if (h > 0) s.g->drawFastVLine(x - s.ox, y - s.oy, h, c);
}

void gFillRect(const Surf& s, int x, int y, int w, int h, uint16_t c) {
    if (w > 0 && h > 0) s.g->fillRect(x - s.ox, y - s.oy, w, h, c);
}

void gDrawRect(const Surf& s, int x, int y, int w, int h, uint16_t c) {
    gHLine(s, x, y, w, c);
    gHLine(s, x, y + h - 1, w, c);
    gVLine(s, x, y, h, c);
    gVLine(s, x + w - 1, y, h, c);
}

static void circleHelper(const Surf& s, int x0, int y0, int r, int cn, uint16_t c) {
    int f = 1 - r, dx = 1, dy = -2 * r, x = 0, y = r;
    while (x < y) {
        if (f >= 0) { y--; dy += 2; f += dy; }
        x++; dx += 2; f += dx;
        if (cn & 4) { gPixel(s, x0 + x, y0 + y, c); gPixel(s, x0 + y, y0 + x, c); }
        if (cn & 2) { gPixel(s, x0 + x, y0 - y, c); gPixel(s, x0 + y, y0 - x, c); }
        if (cn & 8) { gPixel(s, x0 - y, y0 + x, c); gPixel(s, x0 - x, y0 + y, c); }
        if (cn & 1) { gPixel(s, x0 - y, y0 - x, c); gPixel(s, x0 - x, y0 - y, c); }
    }
}

static void fillCircleHelper(const Surf& s, int x0, int y0, int r, int cn, int delta, uint16_t c) {
    int f = 1 - r, dx = 1, dy = -2 * r, x = 0, y = r, px = x, py = y;
    delta++;
    while (x < y) {
        if (f >= 0) { y--; dy += 2; f += dy; }
        x++; dx += 2; f += dx;
        if (x < (y + 1)) {
            if (cn & 1) gVLine(s, x0 + x, y0 - y, 2 * y + delta, c);
            if (cn & 2) gVLine(s, x0 - x, y0 - y, 2 * y + delta, c);
        }
        if (y != py) {
            if (cn & 1) gVLine(s, x0 + py, y0 - px, 2 * px + delta, c);
            if (cn & 2) gVLine(s, x0 - py, y0 - px, 2 * px + delta, c);
            py = y;
        }
        px = x;
    }
}

void gCircle(const Surf& s, int x0, int y0, int r, uint16_t c) {
    gPixel(s, x0, y0 + r, c);
    gPixel(s, x0, y0 - r, c);
    gPixel(s, x0 + r, y0, c);
    gPixel(s, x0 - r, y0, c);
    circleHelper(s, x0, y0, r, 15, c);
}

void gFillCircle(const Surf& s, int x0, int y0, int r, uint16_t c) {
    gVLine(s, x0, y0 - r, 2 * r + 1, c);
    fillCircleHelper(s, x0, y0, r, 3, 0, c);
}

void gRoundRect(const Surf& s, int x, int y, int w, int h, int r, uint16_t c) {
    int m = (w < h ? w : h) / 2;
    if (r > m) r = m;
    gHLine(s, x + r, y, w - 2 * r, c);
    gHLine(s, x + r, y + h - 1, w - 2 * r, c);
    gVLine(s, x, y + r, h - 2 * r, c);
    gVLine(s, x + w - 1, y + r, h - 2 * r, c);
    circleHelper(s, x + r, y + r, r, 1, c);
    circleHelper(s, x + w - r - 1, y + r, r, 2, c);
    circleHelper(s, x + w - r - 1, y + h - r - 1, r, 4, c);
    circleHelper(s, x + r, y + h - r - 1, r, 8, c);
}

void gFillRoundRect(const Surf& s, int x, int y, int w, int h, int r, uint16_t c) {
    int m = (w < h ? w : h) / 2;
    if (r > m) r = m;
    gFillRect(s, x + r, y, w - 2 * r, h, c);
    fillCircleHelper(s, x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c);
    fillCircleHelper(s, x + r, y + r, r, 2, h - 2 * r - 1, c);
}

void gClip(const Surf& s, int x, int y, int w, int h) {
    s.g->setClipRect(x - s.ox, y - s.oy, w, h);
}

void gUnclip(const Surf& s) {
    s.g->clearClipRect();
}

void gText0(const Surf& s, int x, int y, const char* t, uint16_t c) {
    s.g->setFont(&fonts::Font0);
    s.g->setTextSize(1);
    s.g->setTextDatum(textdatum_t::top_left);
    s.g->setTextColor(c);
    s.g->drawString(t, x - s.ox, y - s.oy);
}

int gW0(const char* t) {
    int n = strlen(t);
    return n ? n * 6 - 1 : 0;
}

void gTextTT(const Surf& s, int x, int y, const char* t, uint16_t c) {
    int cx = x;
    int base = y + 5;
    for (; *t; ++t) {
        uint8_t ch = (uint8_t)*t;
        if (ch < 0x20 || ch > 0x7E) continue;
        const TTGlyph& g = TT_GLYPHS[ch - 0x20];
        for (int r = 0; r < g.h; r++) {
            uint8_t bits = g.rows[r];
            for (int k = 0; k < g.w; k++) {
                if (bits & (0x80 >> k)) gPixel(s, cx + g.xo + k, base + g.yo + r, c);
            }
        }
        cx += g.adv;
    }
}

int gWTT(const char* t) {
    int w = 0;
    for (; *t; ++t) {
        uint8_t ch = (uint8_t)*t;
        if (ch < 0x20 || ch > 0x7E) continue;
        w += TT_GLYPHS[ch - 0x20].adv;
    }
    return w ? w - 1 : 0;
}
