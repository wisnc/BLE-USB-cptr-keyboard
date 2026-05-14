#ifndef USBHID_H
#define USBHID_H

#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"
#include <M5Cardputer.h>

extern USBHIDMouse mouse;

void usbMouse();
void usbKeyboard();
void usbScroll(int8_t delta);
void handleUsbMode(bool mouseMode);

#endif
