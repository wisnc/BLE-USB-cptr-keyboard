#include "scroll.h"
#include <Wire.h>

#define SCROLL_ADDR    0x40
#define SCROLL_INC_REG 0x50
#define GROVE_SDA      2
#define GROVE_SCL      1

static bool ready = false;

static int32_t readInc() {
    int32_t val = 0;
    Wire.beginTransmission(SCROLL_ADDR);
    Wire.write(SCROLL_INC_REG);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((uint8_t)SCROLL_ADDR, (uint8_t)4) == 4) {
        uint8_t* p = (uint8_t*)&val;
        for (int i = 0; i < 4; i++) p[i] = Wire.read();
    }
    return val;
}

void scrollBegin() {
    Wire.begin(GROVE_SDA, GROVE_SCL, 400000U);
    Wire.beginTransmission(SCROLL_ADDR);
    ready = (Wire.endTransmission() == 0);
    if (ready) readInc();
}

int32_t scrollRead() {
    return ready ? readInc() : 0;
}
