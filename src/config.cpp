#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

#define KEYBM_DIR    "/.keybm"
#define KEYBM_CONFIG "/.keybm/config"
#define KEYBM_SLOTS  "/.keybm/slots"

Config cfg;
bool sdReady = false;

static void defaults() {
    cfg.theme = 0xF88C00;
    cfg.imuPose = false;
    cfg.imuDead = 3.0f;
    cfg.imuMax = 30.0f;
    cfg.imuSpeed = 12.0f;
    cfg.imuAxisX = 0;
    cfg.imuSignX = -1;
    cfg.imuAxisY = 1;
    cfg.imuSignY = -1;
    cfg.moveSpeed = 2;
    cfg.brightness = 128;
    cfg.brightStep = 8;
    cfg.slot = 1;
    cfg.link = LINK_AUTO;
    cfg.usbWait = 150;
    strcpy(cfg.name, "keybm");
    cfg.scrollDir = 1;
    cfg.debug = false;
}

static void trim(char* s) {
    int n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static bool readLine(File& f, char* buf, int max) {
    if (!f.available()) return false;
    int n = 0;
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\n') break;
        if (n < max - 1) buf[n++] = c;
    }
    buf[n] = 0;
    return true;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool parseAxes(const char* v) {
    if (strlen(v) != 4) return false;
    int8_t ax[2], sg[2];
    for (int i = 0; i < 2; i++) {
        char s = v[i * 2];
        char a = (char)tolower((unsigned char)v[i * 2 + 1]);
        if (s != '+' && s != '-') return false;
        if (a < 'x' || a > 'z') return false;
        sg[i] = (s == '-') ? -1 : 1;
        ax[i] = a - 'x';
    }
    if (ax[0] == ax[1]) return false;
    cfg.imuSignX = sg[0];
    cfg.imuAxisX = ax[0];
    cfg.imuSignY = sg[1];
    cfg.imuAxisY = ax[1];
    return true;
}

static const char* const KEYS[] = {
    "theme", "imu_zero", "imu_dead", "imu_max", "imu_speed", "imu_axes", "move_speed",
    "brightness", "bright_step", "slot", "link", "usb_wait", "name", "scroll_dir", "debug"
};
static const int KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);
static uint32_t seenKeys = 0;

static void apply(const char* k, const char* v) {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (!strcasecmp(k, KEYS[i])) seenKeys |= 1u << i;
    }
    if (!strcasecmp(k, "theme")) {
        const char* p = v;
        if (*p == '#') p++;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        if (strlen(p) == 6) cfg.theme = strtoul(p, nullptr, 16) & 0xFFFFFF;
    } else if (!strcasecmp(k, "imu_zero")) {
        if (!strcasecmp(v, "pose")) cfg.imuPose = true;
        else if (!strcasecmp(v, "level")) cfg.imuPose = false;
    } else if (!strcasecmp(k, "imu_dead")) {
        cfg.imuDead = clampf(atof(v), 0.0f, 30.0f);
    } else if (!strcasecmp(k, "imu_max")) {
        cfg.imuMax = clampf(atof(v), 5.0f, 80.0f);
    } else if (!strcasecmp(k, "imu_speed")) {
        cfg.imuSpeed = clampf(atof(v), 1.0f, 100.0f);
    } else if (!strcasecmp(k, "imu_axes")) {
        parseAxes(v);
    } else if (!strcasecmp(k, "move_speed")) {
        cfg.moveSpeed = clampi(atoi(v), 1, 50);
    } else if (!strcasecmp(k, "brightness")) {
        cfg.brightness = clampi(atoi(v), 1, 255);
    } else if (!strcasecmp(k, "bright_step")) {
        cfg.brightStep = clampi(atoi(v), 1, 64);
    } else if (!strcasecmp(k, "slot")) {
        cfg.slot = clampi(atoi(v), 1, 3);
    } else if (!strcasecmp(k, "link")) {
        if (!strcasecmp(v, "bt")) cfg.link = LINK_BT;
        else if (!strcasecmp(v, "usb")) cfg.link = LINK_USB;
        else if (!strcasecmp(v, "auto")) cfg.link = LINK_AUTO;
    } else if (!strcasecmp(k, "usb_wait")) {
        cfg.usbWait = clampi(atoi(v), 0, 3000);
    } else if (!strcasecmp(k, "name")) {
        if (*v) {
            strncpy(cfg.name, v, sizeof(cfg.name) - 3);
            cfg.name[sizeof(cfg.name) - 3] = 0;
        }
    } else if (!strcasecmp(k, "scroll_dir")) {
        cfg.scrollDir = atoi(v) < 0 ? -1 : 1;
    } else if (!strcasecmp(k, "debug")) {
        cfg.debug = atoi(v) != 0;
    }
}

static const char* linkName(LinkPref l) {
    return l == LINK_BT ? "bt" : (l == LINK_USB ? "usb" : "auto");
}

bool configSave() {
    if (!sdReady) return false;
    if (!SD.exists(KEYBM_DIR)) SD.mkdir(KEYBM_DIR);
    File f = SD.open(KEYBM_CONFIG, FILE_WRITE);
    if (!f) return false;
    char axes[5] = {
        cfg.imuSignX < 0 ? '-' : '+', (char)('x' + cfg.imuAxisX),
        cfg.imuSignY < 0 ? '-' : '+', (char)('x' + cfg.imuAxisY), 0
    };
    f.printf("theme=%06lX\n", (unsigned long)cfg.theme);
    f.printf("imu_zero=%s\n", cfg.imuPose ? "pose" : "level");
    f.printf("imu_dead=%g\n", cfg.imuDead);
    f.printf("imu_max=%g\n", cfg.imuMax);
    f.printf("imu_speed=%g\n", cfg.imuSpeed);
    f.printf("imu_axes=%s\n", axes);
    f.printf("move_speed=%d\n", cfg.moveSpeed);
    f.printf("brightness=%d\n", cfg.brightness);
    f.printf("bright_step=%d\n", cfg.brightStep);
    f.printf("slot=%d\n", cfg.slot);
    f.printf("link=%s\n", linkName(cfg.link));
    f.printf("usb_wait=%d\n", cfg.usbWait);
    f.printf("name=%s\n", cfg.name);
    f.printf("scroll_dir=%d\n", cfg.scrollDir);
    f.printf("debug=%d\n", cfg.debug ? 1 : 0);
    f.close();
    return true;
}

void configBegin() {
    defaults();
    SPI.begin(40, 39, 14, 12);
    sdReady = SD.begin(12, SPI, 25000000);
    if (!sdReady) return;
    if (!SD.exists(KEYBM_DIR)) SD.mkdir(KEYBM_DIR);
    if (!SD.exists(KEYBM_CONFIG)) {
        configSave();
        return;
    }
    File f = SD.open(KEYBM_CONFIG, FILE_READ);
    if (!f) return;
    char line[96];
    while (readLine(f, line, sizeof(line))) {
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = line;
        char* v = eq + 1;
        trim(k);
        trim(v);
        apply(k, v);
    }
    f.close();
    if (seenKeys != (1u << KEY_COUNT) - 1) configSave();
}

bool slotsLoad(SlotState* s) {
    for (int i = 0; i < 3; i++) {
        s[i].gen = 0;
        s[i].suffix[0] = 0;
    }
    if (!sdReady || !SD.exists(KEYBM_SLOTS)) return false;
    File f = SD.open(KEYBM_SLOTS, FILE_READ);
    if (!f) return false;
    char line[48];
    while (readLine(f, line, sizeof(line))) {
        trim(line);
        if (strlen(line) < 3 || line[1] != '=') continue;
        int n = line[0] - '1';
        if (n < 0 || n > 2) continue;
        char* colon = strchr(line + 2, ':');
        if (colon) *colon = 0;
        s[n].gen = (uint8_t)clampi(atoi(line + 2), 0, 255);
        if (colon) {
            strncpy(s[n].suffix, colon + 1, 4);
            s[n].suffix[4] = 0;
            trim(s[n].suffix);
        }
    }
    f.close();
    return true;
}

bool slotsSave(const SlotState* s, int active, const char* address) {
    if (!sdReady) return false;
    if (!SD.exists(KEYBM_DIR)) SD.mkdir(KEYBM_DIR);
    File f = SD.open(KEYBM_SLOTS, FILE_WRITE);
    if (!f) return false;
    for (int i = 0; i < 3; i++) f.printf("%d=%u:%s\n", i + 1, s[i].gen, s[i].suffix);
    if (address && *address) f.printf("active=%d %s\n", active, address);
    f.close();
    return true;
}
