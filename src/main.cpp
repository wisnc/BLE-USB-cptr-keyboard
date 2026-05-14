#include <M5Cardputer.h>
#include <Wire.h>
#include <USB.h>
#include "bluetooth.h"
#include "display.h"
#include "usbHid.h"

bool mouseMode = true;
bool usbMode = true;
bool lastBluetoothStatus = false;

// ---- Unit Scroll (I2C @ 0x40 on Grove Port A) ----
#define SCROLL_ADDR   0x40
#define SCROLL_INC_REG 0x50
#define GROVE_SDA     2
#define GROVE_SCL     1
static bool scrollReady = false;

static int32_t scrollReadInc() {
    int32_t val = 0;
    Wire.beginTransmission(SCROLL_ADDR);
    Wire.write(SCROLL_INC_REG);
    Wire.endTransmission(false);
    if (Wire.requestFrom((uint8_t)SCROLL_ADDR, (uint8_t)4) == 4) {
        uint8_t* p = (uint8_t*)&val;
        for (int i = 0; i < 4; i++) p[i] = Wire.read();
    }
    return val;
}

// ---- Display brightness ----
#define BRIGHT_STEP 32
static uint8_t brightness = 128;

// ---- G0 button long-press tracking ----
#define BTN_G0          0
#define UNPAIR_HOLD_MS  3000
static bool g0WasPressed = false;
static unsigned long g0PressStart = 0;

void selectMode() {
    bool lastMode = !usbMode;
    while (true) {
        M5Cardputer.update();

        if (lastMode != usbMode) {
            displaySelectionScreen(usbMode);
            lastMode = usbMode;
        }

        if (M5Cardputer.Keyboard.isChange()) {
            if (M5Cardputer.Keyboard.isPressed()) {
                Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

                if(M5Cardputer.Keyboard.isKeyPressed('.') || M5Cardputer.Keyboard.isKeyPressed(';')) {
                    usbMode = !usbMode;
                }

                if (status.enter) {
                    break;
                }
            }

        }
        delay(10);
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    
    setupDisplay();
    displayWelcomeScreen();

    selectMode();
    if (usbMode) {
        USB.begin();
    } else {
        initBluetooth();
    }

    // G0 button
    pinMode(BTN_G0, INPUT_PULLUP);

    // Unit Scroll init on Grove Port A
    Wire.begin(GROVE_SDA, GROVE_SCL, 400000U);
    Wire.beginTransmission(SCROLL_ADDR);
    scrollReady = (Wire.endTransmission() == 0);
    if (scrollReady) scrollReadInc();  // flush stale delta

    displayMainScreen(usbMode, mouseMode, getBluetoothStatus());
    M5Cardputer.Display.setBrightness(brightness);
}

void loop() {
    M5Cardputer.update();

    // ---- BT connection status change ----
    auto bluetoothStatus = getBluetoothStatus();
    if (lastBluetoothStatus != bluetoothStatus) {
        modeIndicator(usbMode, bluetoothStatus);
        lastBluetoothStatus = bluetoothStatus;
    }

    // ---- G0 button: short press = toggle, long press = unpair ----
    bool g0Now = (digitalRead(BTN_G0) == LOW);

    if (g0Now && !g0WasPressed) {
        // Just pressed
        g0PressStart = millis();
        g0WasPressed = true;
    }
    else if (g0Now && g0WasPressed) {
        // Still held — check for long press threshold
        if (!usbMode && (millis() - g0PressStart >= UNPAIR_HOLD_MS)) {
            unpairBluetooth();
            displayUnpairMessage();
            delay(2000);
            displayMainScreen(usbMode, mouseMode, getBluetoothStatus());
            g0WasPressed = false;  // consume the press
        }
    }
    else if (!g0Now && g0WasPressed) {
        // Released — if under threshold, it was a short press
        if (millis() - g0PressStart < UNPAIR_HOLD_MS) {
            mouseMode = !mouseMode;
            drawDeviceRect(mouseMode);
        }
        g0WasPressed = false;
    }

    // ---- Unit Scroll polling (active in both modes) ----
    if (scrollReady) {
        int32_t inc = scrollReadInc();
        if (inc != 0) {
            // Clamp to int8_t range
            int8_t delta = 0;
            if (inc > 127) delta = 127;
            else if (inc < -127) delta = -127;
            else delta = (int8_t)inc;

            // Negate: scroll unit positive = physical scroll down
            delta = -delta;

            if (usbMode) {
                usbScroll(delta);
            } else {
                bluetoothScroll(delta);
            }
        }
    }

    // ---- Fn + brightness controls ----
    if (M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
        if (keys.fn) {
            for (auto c : keys.word) {
                if (c == '-') {
                    brightness = (brightness > BRIGHT_STEP) ? brightness - BRIGHT_STEP : 1;
                    M5Cardputer.Display.setBrightness(brightness);
                }
                if (c == '=') {
                    brightness = (brightness <= 255 - BRIGHT_STEP) ? brightness + BRIGHT_STEP : 255;
                    M5Cardputer.Display.setBrightness(brightness);
                }
            }
        }
    }

    // ---- Keyboard/Mouse handling ----
    if (usbMode) {
        handleUsbMode(mouseMode);
    } else {
        handleBluetoothMode(mouseMode);
    }
}
