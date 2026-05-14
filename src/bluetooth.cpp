
#include "bluetooth.h"
#include <esp_gap_ble_api.h>

BLEHIDDevice* hid;
BLECharacteristic* mouseInput;
BLECharacteristic* keyboardInput;
bool bluetoothIsConnected = false;

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

void MyBLEServerCallbacks::onConnect(BLEServer* pServer) {
    bluetoothIsConnected = true;
}

void MyBLEServerCallbacks::onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
    bluetoothIsConnected = false;
    
    pServer->disconnect(param->disconnect.conn_id);
    pServer->startAdvertising();
}

bool getBluetoothStatus() {
    return bluetoothIsConnected;
}

void unpairBluetooth() {
    int devNum = esp_ble_get_bond_device_num();
    if (devNum > 0) {
        esp_ble_bond_dev_t *devList = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * devNum);
        if (devList) {
            esp_ble_get_bond_device_list(&devNum, devList);
            for (int i = 0; i < devNum; i++) {
                esp_ble_remove_bond_device(devList[i].bd_addr);
            }
            free(devList);
        }
    }

    // Disconnect current client if connected
    if (bluetoothIsConnected) {
        bluetoothIsConnected = false;
    }

    // Deinit and reinit to reset state cleanly
    deinitBluetooth();
    delay(500);
    initBluetooth();
}

void bluetoothScroll(int8_t delta) {
    if (!bluetoothIsConnected) return;
    uint8_t report[4] = {0, 0, 0, (uint8_t)delta};
    mouseInput->setValue(report, sizeof(report));
    mouseInput->notify();
}

void bluetoothMouse() {
    int16_t x = 0;
    int16_t y = 0;
    uint8_t buttons = 0;

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    // Left click
    if (status.enter) {
        buttons |= 0x01;
    }
    // Right click
    if (M5Cardputer.Keyboard.isKeyPressed('\\')) {
        buttons |= 0x02;
    }

    // Vertical
    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        y -= 1;
    }
    else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        y += 1;
    }

    // Horizontal
    if (M5Cardputer.Keyboard.isKeyPressed('/')) {
        x += 1;
    }
    else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
        x -= 1;
    }

    // Send (4 bytes: buttons, x, y, wheel=0)
    uint8_t report[4] = {buttons, (uint8_t)x, (uint8_t)y, 0};
    mouseInput->setValue(report, sizeof(report));
    mouseInput->notify();
}

void bluetoothKeyboard() {
    uint8_t modifier = 0;
    uint8_t keycode[6] = {0};

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    // Build keycode array from hid_keys
    int count = 0;
    for (auto key : status.hid_keys) {
        if (count < 6) {
            keycode[count] = key;
            count++;
        }
    }

    if (M5Cardputer.Keyboard.isKeyPressed(' ') && count < 6) {
        keycode[count++] = 0x2C;  // HID SPACE
    }

    // Fn + ;/./,// → arrow keys
    if (status.fn) {
        for (int i = 0; i < count; i++) {
            if (keycode[i] == HID_SEMICOLON) keycode[i] = HID_UP;
            else if (keycode[i] == HID_PERIOD) keycode[i] = HID_DOWN;
            else if (keycode[i] == HID_COMMA)  keycode[i] = HID_LEFT;
            else if (keycode[i] == HID_SLASH)  keycode[i] = HID_RIGHT;
        }
    }

    // Modifiers
    if (status.ctrl)  modifier |= 0x01;  // Left Ctrl
    if (status.shift) modifier |= 0x02;  // Left Shift
    if (status.alt)   modifier |= 0x04;  // Left Alt
    if (status.opt)   modifier |= 0x08;  // Left GUI (Win/Cmd)

    // Send
    uint8_t report[8] = {
        modifier, 0,
        keycode[0], keycode[1], keycode[2],
        keycode[3], keycode[4], keycode[5]
    };
    keyboardInput->setValue(report, sizeof(report));
    keyboardInput->notify();

    delay(50);
}

void sendEmptyReports() {
    uint8_t emptyMouseReport[4] = {0, 0, 0, 0};
    mouseInput->setValue(emptyMouseReport, sizeof(emptyMouseReport));
    mouseInput->notify();

    uint8_t emptyKeyboardReport[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    keyboardInput->setValue(emptyKeyboardReport, sizeof(emptyKeyboardReport));
    keyboardInput->notify();
}

void handleBluetoothMode(bool mouseMode) {
    if (bluetoothIsConnected) {
        if (M5Cardputer.Keyboard.isPressed()) {
            if (mouseMode) {
                bluetoothMouse();
            } else {
                bluetoothKeyboard();
            }
        } else {
            sendEmptyReports();
        }
    }
    delay(7);
}

void initBluetooth() {
    BLEDevice::init("M5-Keyboard-Mouse");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyBLEServerCallbacks());

    hid = new BLEHIDDevice(pServer);
    mouseInput = hid->inputReport(1);
    keyboardInput = hid->inputReport(2);

    hid->manufacturer()->setValue("M5Stack");
    hid->pnp(0x02, 0x1234, 0x5678, 0x0100);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    hid->startServices();

    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->setAppearance(HID_MOUSE);
    pAdvertising->addServiceUUID(hid->hidService()->getUUID());
    pAdvertising->start();

    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
}

void deinitBluetooth() {
    BLEDevice::deinit(); 
    delay(1000);
}
