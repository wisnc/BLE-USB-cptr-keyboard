// Project: keybm
// Author: wisncn@aol.com
// Repo: github.com/wisnc
// Created: 2026-09-25


#include <M5Cardputer.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "hid.h"
#include "imu.h"
#include "scroll.h"
#include "ui.h"

static const int BTN_G0 = 0;
static const uint32_t TICK_MS = 8;
static const uint32_t CLEAR_HOLD_MS = 3000;
static const uint32_t SAVE_DELAY_MS = 1500;
static const uint32_t GAUGE_FRAME_MS = 33;
static const uint32_t WHEEL_FLASH_MS = 120;

static const uint8_t HID_RIGHT = 0x4F;
static const uint8_t HID_LEFT = 0x50;
static const uint8_t HID_DOWN = 0x51;
static const uint8_t HID_UP = 0x52;
static const uint8_t HID_PGUP = 0x4B;
static const uint8_t HID_PGDN = 0x4E;
static const uint8_t HID_SEMICOLON = 0x33;
static const uint8_t HID_PERIOD = 0x37;
static const uint8_t HID_COMMA = 0x36;
static const uint8_t HID_SLASH = 0x38;
static const uint8_t HID_1 = 0x1E;
static const uint8_t HID_0 = 0x27;
static const uint8_t HID_MINUS = 0x2D;
static const uint8_t HID_EQUAL = 0x2E;
static const uint8_t HID_F1 = 0x3A;
static const uint8_t HID_F11 = 0x44;
static const uint8_t HID_F12 = 0x45;

struct Repeat {
    bool held = false;
    uint32_t next = 0;
    bool fire(bool down, uint32_t now, uint32_t first, uint32_t rate) {
        if (!down) {
            held = false;
            return false;
        }
        if (!held) {
            held = true;
            next = now + first;
            return true;
        }
        if ((int32_t)(now - next) >= 0) {
            next = now + rate;
            return true;
        }
        return false;
    }
};

static bool mouseMode = true;
static bool usbLink = false;
static uint32_t lastTick = 0;

static bool down[256];
static bool prevDown[256];
static uint16_t rows[4];

static bool g0Prev = false;
static bool g0Consumed = false;
static uint32_t g0Start = 0;

static uint8_t lastButtons = 0;
static int wheelAcc = 0;
static uint32_t wheelUpUntil = 0;
static uint32_t wheelDownUntil = 0;
static float accX = 0, accY = 0;
static float imuNx = 0, imuNy = 0;
static uint32_t lastGauge = 0;
static Repeat repWheelUp, repWheelDown, repBright, repDim;

static uint8_t lastMods = 0;
static uint8_t lastKeys[6] = { 0 };
static int pendingTaps = 0;
static bool tapOn = false;
static uint8_t tapKey = 0;

static bool brightDirty = false;
static uint32_t brightChanged = 0;

static bool edge(uint8_t v) {
    return down[v] && !prevDown[v];
}

static void scanKeys() {
    memcpy(prevDown, down, sizeof(down));
    memset(down, 0, sizeof(down));
    memset(rows, 0, sizeof(rows));
    for (const auto& p : M5Cardputer.Keyboard.keyList()) {
        if (p.x < 0 || p.x > 13 || p.y < 0 || p.y > 3) continue;
        uint8_t v = (uint8_t)M5Cardputer.Keyboard.getKeyValue(p).value_first;
        down[v] = true;
        rows[p.y] |= (uint16_t)(1 << p.x);
    }
}

static int8_t clamp8(int v) {
    return (int8_t)(v > 127 ? 127 : (v < -127 ? -127 : v));
}

static void resetInputState() {
    hidRelease();
    lastButtons = 0;
    wheelAcc = 0;
    accX = accY = 0;
    lastMods = 0;
    memset(lastKeys, 0, sizeof(lastKeys));
    pendingTaps = 0;
    tapOn = false;
}

static void showMode() {
    if (mouseMode) uiMouseScreen();
    else uiKeyboardScreen();
    uiStatus(mouseMode, usbLink, cfg.slot, hidLinked());
}

static void toggleMode() {
    if (mouseMode) {
        imuSetEnabled(false);
        imuNx = imuNy = 0;
    }
    mouseMode = !mouseMode;
    resetInputState();
    showMode();
}

static void restartWith(const char* a, const char* b, uint32_t hold) {
    resetInputState();
    uiMessage(a, b);
    delay(hold);
    esp_restart();
}

static void clearSlot() {
    char a[24];
    snprintf(a, sizeof(a), "slot %d cleared", cfg.slot);
    resetInputState();
    hidClearSlot(cfg.slot);
    restartWith(a, "restarting", 1200);
}

static void switchSlot(int n) {
    if (n == cfg.slot) return;
    if (!sdReady) {
        uiMessage("no sd card", "slot stays");
        delay(1200);
        showMode();
        return;
    }
    int prev = cfg.slot;
    cfg.slot = n;
    if (!configSave()) {
        cfg.slot = prev;
        uiMessage("sd write failed", "slot stays");
        delay(1200);
        showMode();
        return;
    }
    char a[16];
    snprintf(a, sizeof(a), "slot %d", n);
    restartWith(a, "restarting", 600);
}

static void changeBrightness(int dir, uint32_t now) {
    int b = cfg.brightness + dir * cfg.brightStep;
    if (b < 1) b = 1;
    if (b > 255) b = 255;
    if (b == cfg.brightness) return;
    cfg.brightness = b;
    M5.Display.setBrightness((uint8_t)b);
    brightDirty = true;
    brightChanged = now;
}

static void handleG0(uint32_t now) {
    bool g0 = digitalRead(BTN_G0) == LOW;
    if (g0 && !g0Prev) {
        g0Start = now;
        g0Consumed = false;
    }
    if (g0 && !g0Consumed && !usbLink && now - g0Start >= CLEAR_HOLD_MS) {
        g0Consumed = true;
        clearSlot();
    }
    if (!g0 && g0Prev && !g0Consumed && now - g0Start < CLEAR_HOLD_MS) toggleMode();
    g0Prev = g0;
}

static void mouseStep(uint32_t now, bool tick, int32_t scroll) {
    uint8_t buttons = 0;
    if (down[KEY_ENTER]) buttons |= MB_L;
    if (down['\\']) buttons |= MB_R;
    if (down['\'']) buttons |= MB_M;
    if (down['[']) buttons |= MB_4;
    if (down['-']) buttons |= MB_5;
    if (buttons != lastButtons) {
        hidMouse(buttons, 0, 0, 0);
        lastButtons = buttons;
    }

    if (repWheelUp.fire(down['='], now, 300, 60)) {
        wheelAcc++;
        wheelUpUntil = now + WHEEL_FLASH_MS;
    }
    if (repWheelDown.fire(down[']'], now, 300, 60)) {
        wheelAcc--;
        wheelDownUntil = now + WHEEL_FLASH_MS;
    }
    if (scroll) {
        wheelAcc -= scroll;
        if (scroll > 0) wheelDownUntil = now + WHEEL_FLASH_MS;
        else wheelUpUntil = now + WHEEL_FLASH_MS;
    }

    if (repBright.fire(down['m'], now, 350, 80)) changeBrightness(1, now);
    if (repDim.fire(down['n'], now, 350, 80)) changeBrightness(-1, now);

    if (edge('i')) {
        bool want = !imuEnabled();
        imuSetEnabled(want);
        imuNx = imuNy = 0;
        accX = accY = 0;
        uiGauge(imuEnabled(), 0, 0);
    }

    if (!usbLink) {
        if (edge('1')) switchSlot(1);
        else if (edge('2')) switchSlot(2);
        else if (edge('3')) switchSlot(3);
    }

    if (tick) {
        int dx = 0, dy = 0;
        if (down['/']) dx += cfg.moveSpeed;
        if (down[',']) dx -= cfg.moveSpeed;
        if (down['.']) dy += cfg.moveSpeed;
        if (down[';']) dy -= cfg.moveSpeed;
        if (imuEnabled() && imuRead(imuNx, imuNy)) {
            accX += imuNx * cfg.imuSpeed;
            accY += imuNy * cfg.imuSpeed;
            int ix = (int)accX;
            int iy = (int)accY;
            accX -= ix;
            accY -= iy;
            dx += ix;
            dy += iy;
        }
        int w = wheelAcc;
        wheelAcc = 0;
        if (dx || dy || w) hidMouse(buttons, clamp8(dx), clamp8(dy), clamp8(w));
    }

    if (imuEnabled() && now - lastGauge >= GAUGE_FRAME_MS) {
        lastGauge = now;
        uiGauge(true, imuNx, imuNy);
    }

    uint8_t bits = buttons;
    if (down['='] || (int32_t)(wheelUpUntil - now) > 0) bits |= MB_WU;
    if (down[']'] || (int32_t)(wheelDownUntil - now) > 0) bits |= MB_WD;
    uiMouse(bits);
}

static uint8_t fnMap(uint8_t k) {
    if (k >= HID_1 && k <= HID_0) return HID_F1 + (k - HID_1);
    switch (k) {
        case HID_MINUS: return HID_F11;
        case HID_EQUAL: return HID_F12;
        case HID_SEMICOLON: return HID_UP;
        case HID_PERIOD: return HID_DOWN;
        case HID_COMMA: return HID_LEFT;
        case HID_SLASH: return HID_RIGHT;
        default: return k;
    }
}

static void keyboardStep(bool tick, int32_t scroll) {
    if (scroll) {
        pendingTaps += scroll;
        if (pendingTaps > 8) pendingTaps = 8;
        if (pendingTaps < -8) pendingTaps = -8;
    }
    if (!hidLinked()) {
        pendingTaps = 0;
        tapOn = false;
    }
    if (tick) {
        if (tapOn) {
            tapOn = false;
        } else if (pendingTaps) {
            tapOn = true;
            tapKey = pendingTaps > 0 ? HID_PGDN : HID_PGUP;
            pendingTaps += pendingTaps > 0 ? -1 : 1;
        }
    }

    auto& ks = M5Cardputer.Keyboard.keysState();
    uint8_t mods = ks.modifiers | (ks.opt ? 0x08 : 0);
    uint8_t keys[6] = { 0 };
    int n = 0;
    for (auto k : ks.hid_keys) {
        if (n >= 6) break;
        keys[n++] = ks.fn ? fnMap(k) : k;
    }
    if (tapOn && n < 6) keys[n++] = tapKey;

    if (mods != lastMods || memcmp(keys, lastKeys, 6)) {
        hidKeyboard(mods, keys);
        lastMods = mods;
        memcpy(lastKeys, keys, 6);
    }

    uiKeys(rows);
}

void setup() {
    hidBeginUsb();
    uint32_t usbStart = millis();

    auto m5cfg = M5.config();
    m5cfg.internal_imu = false;
    M5Cardputer.begin(m5cfg, true);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    pinMode(BTN_G0, INPUT_PULLUP);

    configBegin();
    M5.Display.setBrightness((uint8_t)cfg.brightness);
    uiBegin(cfg.theme);

    if (cfg.link == LINK_USB) {
        usbLink = true;
    } else if (cfg.link == LINK_BT) {
        usbLink = false;
    } else {
        while (!hidUsbHostSeen() && millis() - usbStart < (uint32_t)cfg.usbWait) delay(2);
        usbLink = hidUsbHostSeen();
    }
    hidUseUsb(usbLink);
    if (!usbLink) hidStartBle(cfg.name, cfg.slot);

    scrollBegin();
    showMode();
    lastTick = millis();
}

void loop() {
    M5Cardputer.update();
    uint32_t now = millis();

    scanKeys();
    handleG0(now);

    bool tick = now - lastTick >= TICK_MS;
    int32_t scroll = 0;
    if (tick) {
        lastTick = now;
        scroll = scrollRead() * cfg.scrollDir;
    }

    if (mouseMode) mouseStep(now, tick, scroll);
    else keyboardStep(tick, scroll);

    if (hidTakeAuthEvent()) hidSyncBonds(cfg.slot);

    if (brightDirty && now - brightChanged >= SAVE_DELAY_MS) {
        brightDirty = false;
        configSave();
    }

    uiStatus(mouseMode, usbLink, cfg.slot, hidLinked());
    delay(1);
}
