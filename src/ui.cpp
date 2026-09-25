#include "ui.h"
#include "gfx.h"
#include "keymap.h"
#include <M5Cardputer.h>
#include <math.h>
#include <string.h>

static uint16_t T, D45, D30;
static const uint16_t BK = 0x0000;

static M5Canvas gaugeSpr(&M5.Display);
static M5Canvas mouseSpr(&M5.Display);
static bool gaugeSprOk = false;
static bool mouseSprOk = false;

static const int GAUGE_X = 22, GAUGE_Y = 36, GAUGE_S = 81;
static const int GAUGE_CX = 62, GAUGE_CY = 76, GAUGE_R = 40, GAUGE_REACH = 32;
static const int MOUSE_X = 145, MOUSE_Y = 30, MOUSE_W = 62, MOUSE_H = 93;

static int screen = 0;
static uint16_t keyRows[KB_ROWS];
static bool stValid = false, stMouse = false, stUsb = false, stLinked = false;
static int stSlot = 0;
static bool gValid = false, gOn = false;
static int gDx = 0, gDy = 0;
static bool mValid = false;
static uint8_t mBits = 0;

static Surf display() {
    return Surf{ &M5.Display, 0, 0 };
}

static uint16_t rgb(int r, int g, int b) {
    return M5.Display.color565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

void uiBegin(uint32_t themeRgb) {
    int r = (themeRgb >> 16) & 0xF8;
    int g = (themeRgb >> 8) & 0xFC;
    int b = themeRgb & 0xF8;
    T = rgb(r, g, b);
    D45 = rgb((int)(r * 0.45f), (int)(g * 0.45f), (int)(b * 0.45f));
    D30 = rgb((int)(r * 0.3f), (int)(g * 0.3f), (int)(b * 0.3f));
    gaugeSpr.setColorDepth(16);
    gaugeSprOk = gaugeSpr.createSprite(GAUGE_S, GAUGE_S) != nullptr;
    mouseSpr.setColorDepth(16);
    mouseSprOk = mouseSpr.createSprite(MOUSE_W, MOUSE_H) != nullptr;
}

static void frame() {
    Surf s = display();
    gDrawRect(s, 5, 5, 230, 125, T);
    gDrawRect(s, 6, 6, 228, 123, T);
}

static void drawKey(int r, int c, bool on) {
    Surf s = display();
    int x = KB_X[r][c];
    int y = KB_TOP + r * KB_PITCH;
    int w = KB_W[r][c];
    int h = KB_KEY_H;
    gFillRect(s, x, y, w, h, BK);
    if (on) gFillRoundRect(s, x, y, w, h, 2, T);
    else gRoundRect(s, x, y, w, h, 2, T);
    uint16_t col = on ? BK : T;
    const char* lab = KB_LABEL[r][c];
    if (strlen(lab) == 1) {
        gText0(s, x + (w - 5) / 2, y + (h - 7) / 2, lab, col);
    } else {
        gTextTT(s, x + (w - gWTT(lab)) / 2, y + (h - 5) / 2, lab, col);
    }
}

void uiKeyboardScreen() {
    M5.Display.fillScreen(BK);
    frame();
    for (int r = 0; r < KB_ROWS; r++) {
        keyRows[r] = 0;
        for (int c = 0; c < KB_COLS; c++) drawKey(r, c, false);
    }
    stValid = false;
    screen = 1;
}

void uiKeys(const uint16_t rows[4]) {
    if (screen != 1) return;
    for (int r = 0; r < KB_ROWS; r++) {
        uint16_t diff = rows[r] ^ keyRows[r];
        if (!diff) continue;
        for (int c = 0; c < KB_COLS; c++) {
            if (diff & (1 << c)) drawKey(r, c, rows[r] & (1 << c));
        }
        keyRows[r] = rows[r];
    }
}

void uiStatus(bool mouseMode, bool usb, int slot, bool linked) {
    if (screen == 0) return;
    if (stValid && stMouse == mouseMode && stUsb == usb && stSlot == slot && stLinked == linked) return;
    stValid = true;
    stMouse = mouseMode;
    stUsb = usb;
    stSlot = slot;
    stLinked = linked;
    Surf s = display();
    gFillRect(s, 7, 7, 226, 17, BK);
    gText0(s, 11, 11, mouseMode ? "mouse" : "keyboard", T);
    gHLine(s, 11, 24, 218, D45);
    if (usb) {
        gText0(s, 229 - gW0("usb"), 11, "usb", T);
        return;
    }
    int x = 228;
    for (int n = 3; n >= 1; n--) {
        int bx = x - 10;
        char d[2] = { (char)('0' + n), 0 };
        if (n == slot) {
            if (linked) {
                gFillRoundRect(s, bx, 10, 11, 10, 2, T);
                gText0(s, bx + 3, 11, d, BK);
            } else {
                gRoundRect(s, bx, 10, 11, 10, 2, T);
                gText0(s, bx + 3, 11, d, T);
            }
        } else {
            gText0(s, bx + 3, 11, d, D45);
        }
        x = bx - 2;
    }
    gText0(s, x - 2 - gW0("bt"), 11, "bt", T);
}

static void drawGauge(const Surf& s, bool on, int dx, int dy) {
    gFillRect(s, GAUGE_X, GAUGE_Y, GAUGE_S, GAUGE_S, BK);
    if (on) {
        gCircle(s, GAUGE_CX, GAUGE_CY, GAUGE_R, T);
        gHLine(s, GAUGE_CX - GAUGE_R + 4, GAUGE_CY, 2 * GAUGE_R - 7, D30);
        gVLine(s, GAUGE_CX, GAUGE_CY - GAUGE_R + 4, 2 * GAUGE_R - 7, D30);
        gCircle(s, GAUGE_CX, GAUGE_CY, 6, D30);
        gFillCircle(s, GAUGE_CX + dx, GAUGE_CY + dy, 4, T);
    } else {
        gCircle(s, GAUGE_CX, GAUGE_CY, GAUGE_R, D45);
        int w = gW0("imu off");
        gText0(s, GAUGE_CX - w / 2, GAUGE_CY - 3, "imu off", D45);
    }
}

void uiGauge(bool on, float fx, float fy) {
    if (screen != 2) return;
    float len = sqrtf(fx * fx + fy * fy);
    if (len > 1.0f) {
        fx /= len;
        fy /= len;
    }
    int dx = on ? (int)lroundf(fx * GAUGE_REACH) : 0;
    int dy = on ? (int)lroundf(fy * GAUGE_REACH) : 0;
    if (gValid && gOn == on && gDx == dx && gDy == dy) return;
    gValid = true;
    gOn = on;
    gDx = dx;
    gDy = dy;
    if (gaugeSprOk) {
        Surf s{ &gaugeSpr, GAUGE_X, GAUGE_Y };
        drawGauge(s, on, dx, dy);
        gaugeSpr.pushSprite(GAUGE_X, GAUGE_Y);
    } else {
        drawGauge(display(), on, dx, dy);
    }
}

static void drawMouse(const Surf& s, uint8_t bits) {
    const int x = 157, y = 32, w = 47, h = 89, r = 22, split = 70;
    const int cx = x + w / 2;
    gFillRect(s, MOUSE_X, MOUSE_Y, MOUSE_W, MOUSE_H, BK);
    if (bits & MB_L) {
        gClip(s, x, y, cx - x, split - y);
        gFillRoundRect(s, x, y, w, h, r, T);
        gUnclip(s);
    }
    if (bits & MB_R) {
        gClip(s, cx + 1, y, x + w - 1 - cx, split - y);
        gFillRoundRect(s, x, y, w, h, r, T);
        gUnclip(s);
    }
    gRoundRect(s, x, y, w, h, r, T);
    gHLine(s, x, split, w, T);
    gVLine(s, cx, y, split - y, T);
    const int wx = cx - 4, wy = 42, ww = 9, wh = 17;
    gFillRoundRect(s, wx - 1, wy - 1, ww + 2, wh + 2, 4, BK);
    if (bits & MB_M) {
        gFillRoundRect(s, wx, wy, ww, wh, 4, T);
    } else {
        if (bits & MB_WU) {
            gClip(s, wx, wy, ww, wh / 2);
            gFillRoundRect(s, wx, wy, ww, wh, 4, T);
            gUnclip(s);
        }
        if (bits & MB_WD) {
            gClip(s, wx, wy + wh / 2 + 1, ww, wh / 2);
            gFillRoundRect(s, wx, wy, ww, wh, 4, T);
            gUnclip(s);
        }
        gRoundRect(s, wx, wy, ww, wh, 4, T);
        gHLine(s, wx + 2, wy + wh / 2, ww - 4, T);
    }
    struct Side { const char* lab; int by; uint8_t bit; };
    const Side sides[2] = { { "5", 74, MB_5 }, { "4", 88, MB_4 } };
    for (const Side& sd : sides) {
        if (bits & sd.bit) gFillRoundRect(s, x - 5, sd.by, 5, 11, 1, T);
        else gRoundRect(s, x - 5, sd.by, 5, 11, 1, T);
        gTextTT(s, x - 10, sd.by + 3, sd.lab, T);
    }
}

void uiMouse(uint8_t bits) {
    if (screen != 2) return;
    if (mValid && mBits == bits) return;
    mValid = true;
    mBits = bits;
    if (mouseSprOk) {
        Surf s{ &mouseSpr, MOUSE_X, MOUSE_Y };
        drawMouse(s, bits);
        mouseSpr.pushSprite(MOUSE_X, MOUSE_Y);
    } else {
        drawMouse(display(), bits);
    }
}

void uiMouseScreen() {
    M5.Display.fillScreen(BK);
    frame();
    stValid = false;
    gValid = false;
    mValid = false;
    screen = 2;
    uiGauge(false, 0, 0);
    uiMouse(0);
}

void uiMessage(const char* a, const char* b) {
    M5.Display.fillScreen(BK);
    frame();
    screen = 0;
    stValid = false;
    Surf s = display();
    if (a) gText0(s, 120 - gW0(a) / 2, 56, a, T);
    if (b) gText0(s, 120 - gW0(b) / 2, 70, b, D45);
}
