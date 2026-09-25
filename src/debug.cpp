#include "debug.h"
#include "config.h"
#include "gfx.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <nvs.h>
#include "hid.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

DebugState dbg = {};

struct DbgEntry {
    uint32_t ms;
    bool alert;
    char text[32];
};

static const int LOG_SIZE = 16;
static const int LOG_LINES = 6;
static const int LINE_X = 7, LINE_W = 226, LINE_H = 9, TOP = 9, RULE_Y = 72, LOG_TOP = 74, HEAD_LINES = 7;
static DbgEntry logBuf[LOG_SIZE];
static volatile uint32_t logSeq = 0;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

static M5Canvas lineSpr(&M5.Display);
static bool lineSprOk = false;
static uint16_t cT, cD, cW;
static uint32_t drawnSeq = 0xFFFFFFFF;
static uint32_t lastHeader = 0;
static char headerCache[HEAD_LINES][64];

void dbgAddr(char* out, const uint8_t* a) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

void dbgBootCounters() {
    nvs_handle_t h;
    uint32_t n = 0;
    if (nvs_open("kbdbg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &n);
        n++;
        nvs_set_u32(h, "boots", n);
        nvs_commit(h);
        nvs_close(h);
    }
    dbg.nvsBoots = (int)n;
    dbg.sdBoots = 0;
    if (!sdReady) return;
    int s = 0;
    File f = SD.open("/.keybm/boots", FILE_READ);
    if (f) {
        char b[12] = { 0 };
        f.read((uint8_t*)b, sizeof(b) - 1);
        f.close();
        s = atoi(b);
    }
    s++;
    f = SD.open("/.keybm/boots", FILE_WRITE);
    if (f) {
        f.printf("%d\n", s);
        f.close();
    }
    dbg.sdBoots = s;
}

void dbgLog(bool alert, const char* fmt, ...) {
    char t[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof(t), fmt, ap);
    va_end(ap);
    uint32_t ms = millis();
    portENTER_CRITICAL(&logMux);
    DbgEntry& e = logBuf[logSeq % LOG_SIZE];
    e.ms = ms;
    e.alert = alert;
    memcpy(e.text, t, sizeof(t));
    logSeq++;
    portEXIT_CRITICAL(&logMux);
}

static void colors() {
    int r = (cfg.theme >> 16) & 0xF8;
    int g = (cfg.theme >> 8) & 0xFC;
    int b = cfg.theme & 0xF8;
    cT = M5.Display.color565(r, g, b);
    cD = M5.Display.color565((int)(r * 0.45f), (int)(g * 0.45f), (int)(b * 0.45f));
    cW = M5.Display.color565(255, 255, 255);
}

static int lineY(int i) {
    return i < HEAD_LINES ? TOP + i * LINE_H : LOG_TOP + (i - HEAD_LINES) * LINE_H;
}

static void drawLine(int i, const char* a, uint16_t ca, const char* b, uint16_t cb) {
    int y = lineY(i);
    if (lineSprOk) {
        lineSpr.fillScreen(0);
        Surf s{ &lineSpr, LINE_X, y };
        if (a) gText0(s, 10, y, a, ca);
        if (b) gText0(s, 10 + (a ? (int)strlen(a) * 6 : 0), y, b, cb);
        lineSpr.pushSprite(LINE_X, y);
    } else {
        Surf s{ &M5.Display, 0, 0 };
        gFillRect(s, LINE_X, y, LINE_W, LINE_H, 0);
        if (a) gText0(s, 10, y, a, ca);
        if (b) gText0(s, 10 + (a ? (int)strlen(a) * 6 : 0), y, b, cb);
    }
}

void dbgScreen() {
    colors();
    if (!lineSprOk) {
        lineSpr.setColorDepth(16);
        lineSprOk = lineSpr.createSprite(LINE_W, LINE_H) != nullptr;
    }
    M5.Display.fillScreen(0);
    Surf s{ &M5.Display, 0, 0 };
    gDrawRect(s, 5, 5, 230, 125, cT);
    gDrawRect(s, 6, 6, 228, 123, cT);
    gHLine(s, 10, RULE_Y, 220, cD);
    drawnSeq = 0xFFFFFFFF;
    lastHeader = 0;
    memset(headerCache, 0, sizeof(headerCache));
}

static void header(int i, const char* text) {
    char t[37];
    strncpy(t, text, 36);
    t[36] = 0;
    if (!strcmp(headerCache[i], t)) return;
    strcpy(headerCache[i], t);
    drawLine(i, t, cT, nullptr, 0);
}

void dbgUpdate(bool mouseMode, bool usb, uint32_t now) {
    if (now - lastHeader >= 250 || lastHeader == 0) {
        lastHeader = now ? now : 1;
        char l[64];
        if (usb) snprintf(l, sizeof(l), "usb %-8s        up %lus", mouseMode ? "mouse" : "keyboard", (unsigned long)(now / 1000));
        else snprintf(l, sizeof(l), "bt s%d g%d %-8s   up %lus", dbg.slot, dbg.gen, mouseMode ? "mouse" : "keyboard", (unsigned long)(now / 1000));
        header(0, l);
        snprintf(l, sizeof(l), "me %s", dbg.addr[0] ? dbg.addr : "-");
        header(1, l);
        snprintf(l, sizeof(l), "%.36s", dbg.name[0] ? dbg.name : "-");
        header(2, l);
        nvs_stats_t st;
        int used = 0, total = 0;
        if (nvs_get_stats(NULL, &st) == ESP_OK) {
            used = (int)st.used_entries;
            total = (int)st.total_entries;
        }
        snprintf(l, sizeof(l), "boots nvs %d sd %d  nvs %d/%d", dbg.nvsBoots, dbg.sdBoots, used, total);
        header(3, l);
        int our = 0, peer = 0, cccd = 0;
        char first[20] = "-";
        if (!usb) hidStoreInfo(our, peer, cccd, first);
        const char* ld = dbg.loadState < 0 ? "none" : (dbg.loadState ? "ok" : "bad");
        const char* sv = dbg.saveState < 0 ? "-" : (dbg.saveState ? "ok" : "fail");
        snprintf(l, sizeof(l), "sd ld %s sv %s o%d p%d c%d", ld, sv, our, peer, cccd);
        header(4, l);
        if (dbg.connected) snprintf(l, sizeof(l), "peer %s %s", dbg.peer, dbg.encrypted ? "enc" : "open");
        else snprintf(l, sizeof(l), "peer -");
        header(5, l);
        snprintf(l, sizeof(l), "bond %s", first);
        header(6, l);
    }

    uint32_t seq = logSeq;
    if (seq == drawnSeq) return;
    drawnSeq = seq;
    DbgEntry snap[LOG_LINES];
    int n = seq < (uint32_t)LOG_LINES ? (int)seq : LOG_LINES;
    portENTER_CRITICAL(&logMux);
    for (int i = 0; i < n; i++) snap[i] = logBuf[(seq - n + i) % LOG_SIZE];
    portEXIT_CRITICAL(&logMux);
    for (int i = 0; i < LOG_LINES; i++) {
        if (i < n) {
            char ts[12];
            snprintf(ts, sizeof(ts), "%6.1f ", snap[i].ms / 1000.0f);
            drawLine(HEAD_LINES + i, ts, cD, snap[i].text, snap[i].alert ? cW : cT);
        } else {
            drawLine(HEAD_LINES + i, nullptr, 0, nullptr, 0);
        }
    }
}
