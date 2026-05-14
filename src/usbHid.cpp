#include "usbHid.h"

USBHIDMouse mouse;
USBHIDKeyboard keyboard;

// HID keycodes for arrow keys
#define HID_UP    0x52
#define HID_DOWN  0x51
#define HID_LEFT  0x50
#define HID_RIGHT 0x4F

// HID keycodes for ;  .  ,  /
#define HID_SEMICOLON 0x33
#define HID_PERIOD    0x37
#define HID_COMMA     0x36
#define HID_SLASH     0x38

static bool mouseInited = false;
static bool kbInited = false;

void usbScroll(int8_t delta) {
    if (!mouseInited) { mouse.begin(); mouseInited = true; }
    mouse.move(0, 0, delta);
}

void handleUsbMode(bool mouseMode) {
    if (mouseMode) {
        usbMouse();
    } else  {
        usbKeyboard();
    }
    delay(5);
}

void usbMouse() {
    if (!mouseInited) { mouse.begin(); mouseInited = true; }

    int moveX = 0;
    int moveY = 0;
    if (M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

        if (M5Cardputer.Keyboard.isKeyPressed('/')) {
            moveX = 1;
        } 
        
        if (M5Cardputer.Keyboard.isKeyPressed(',')) {
            moveX = -1;
        }  
        
        if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            moveY = -1;
        } 
        
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            moveY = 1;
        }

        if (status.enter) {
            mouse.press(MOUSE_BUTTON_LEFT);
        } else if (M5Cardputer.Keyboard.isKeyPressed('\\')) {
            mouse.press(MOUSE_BUTTON_RIGHT);
        }

        mouse.move(moveX, moveY);

    } else {
        mouse.release(MOUSE_BUTTON_LEFT);
        mouse.release(MOUSE_BUTTON_RIGHT);
    }
}

void usbKeyboard() {
    if (!kbInited) { keyboard.begin(); kbInited = true; }

    if (!M5Cardputer.Keyboard.isChange()) return;

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    KeyReport report = {0};
    report.modifiers = status.modifiers;

    // Opt → Left GUI (Win/Cmd)
    if (status.opt) report.modifiers |= 0x08;

    uint8_t idx = 0;
    for (auto k : status.hid_keys) {
        if (idx < 6) report.keys[idx++] = k;
        else break;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
        const uint8_t HID_SPACE = 0x2C;
        bool present = false;
        for (uint8_t i = 0; i < idx; ++i) if (report.keys[i] == HID_SPACE) { present = true; break; }
        if (!present && idx < 6) report.keys[idx++] = HID_SPACE;
    }

    // Fn + ;/./,// → arrow keys
    if (status.fn) {
        for (uint8_t i = 0; i < idx; i++) {
            if (report.keys[i] == HID_SEMICOLON) report.keys[i] = HID_UP;
            else if (report.keys[i] == HID_PERIOD) report.keys[i] = HID_DOWN;
            else if (report.keys[i] == HID_COMMA)  report.keys[i] = HID_LEFT;
            else if (report.keys[i] == HID_SLASH)  report.keys[i] = HID_RIGHT;
        }
    }

    if (idx == 0 && report.modifiers == 0) {
        keyboard.releaseAll();
    } else {
        keyboard.sendReport(&report);
    }
}
