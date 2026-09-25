#include "hid.h"
#include "config.h"
#include <Arduino.h>
#include <USB.h>
#include <USBHIDMouse.h>
#include <USBHIDKeyboard.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <HIDTypes.h>
#include <esp_gap_ble_api.h>
#include <esp_mac.h>
#include <Preferences.h>
#include <string.h>

extern "C" bool tud_connected(void);

static USBHIDMouse usbMouse;
static USBHIDKeyboard usbKeyboard;
static bool useUsb = false;
static uint8_t usbButtons = 0;

static BLEHIDDevice* bleHid = nullptr;
static BLECharacteristic* bleMouseIn = nullptr;
static BLECharacteristic* bleKeyIn = nullptr;
static bool bleStarted = false;
static volatile bool bleConnected = false;
static volatile bool authEvent = false;

static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x85, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0
};

class ServerCb : public BLEServerCallbacks {
    void onConnect(BLEServer* s) override {
        bleConnected = true;
    }
    void onDisconnect(BLEServer* s) override {
        bleConnected = false;
        s->startAdvertising();
    }
};

class SecurityCb : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }
    void onPassKeyNotify(uint32_t) override {}
    bool onSecurityRequest() override { return true; }
    bool onConfirmPIN(uint32_t) override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t c) override {
        if (c.success) authEvent = true;
    }
};

void hidBeginUsb() {
    USB.productName("keybm");
    USB.manufacturerName("M5Stack");
    usbMouse.begin();
    usbKeyboard.begin();
    USB.begin();
}

bool hidUsbHostSeen() {
    return tud_connected();
}

static const char* genKey(int slot) {
    return slot == 1 ? "gen1" : (slot == 2 ? "gen2" : "gen3");
}

static uint8_t slotGen(int slot) {
    Preferences p;
    if (!p.begin("keybm", true)) return 0;
    uint8_t g = p.getUChar(genKey(slot), 0);
    p.end();
    return g;
}

static void bumpGen(int slot) {
    Preferences p;
    if (!p.begin("keybm", false)) return;
    p.putUChar(genKey(slot), (uint8_t)(p.getUChar(genKey(slot), 0) + 1));
    p.end();
}

static void applySlotAddress(int slot) {
    uint8_t gen = slotGen(slot);
    if (slot <= 1 && gen == 0) return;
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return;
    mac[0] = (uint8_t)((mac[0] | 0x02) & 0xFE);
    mac[4] ^= (uint8_t)(slot << 4);
    mac[3] ^= gen;
    esp_base_mac_addr_set(mac);
}

void hidStartBle(const char* name, int slot) {
    applySlotAddress(slot);

    char full[32];
    snprintf(full, sizeof(full), "%s %d", name, slot);
    BLEDevice::init(full);
    BLEDevice::setSecurityCallbacks(new SecurityCb());

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ServerCb());

    bleHid = new BLEHIDDevice(server);
    bleMouseIn = bleHid->inputReport(1);
    bleKeyIn = bleHid->inputReport(2);
    bleHid->outputReport(2);
    bleHid->manufacturer()->setValue("M5Stack");
    bleHid->pnp(0x02, 0x1234, 0x5678, 0x0100);
    bleHid->hidInfo(0x00, 0x01);
    bleHid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    bleHid->startServices();

    BLESecurity* sec = new BLESecurity();
    sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
    sec->setCapability(ESP_IO_CAP_NONE);
    sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEAdvertising* adv = server->getAdvertising();
    adv->setAppearance(HID_MOUSE);
    adv->addServiceUUID(bleHid->hidService()->getUUID());
    adv->start();
    bleStarted = true;
}

void hidUseUsb(bool usb) {
    useUsb = usb;
}

bool hidLinked() {
    if (useUsb) return (bool)USB;
    return bleStarted && bleConnected;
}

void hidMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
    if (useUsb) {
        if (buttons != usbButtons) {
            uint8_t rel = usbButtons & ~buttons;
            uint8_t prs = buttons & ~usbButtons;
            if (rel) usbMouse.release(rel);
            if (prs) usbMouse.press(prs);
            usbButtons = buttons;
        }
        if (x || y || wheel) usbMouse.move(x, y, wheel);
        return;
    }
    if (!bleStarted || !bleConnected) return;
    uint8_t r[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel };
    bleMouseIn->setValue(r, sizeof(r));
    bleMouseIn->notify();
}

void hidKeyboard(uint8_t mods, const uint8_t keys[6]) {
    if (useUsb) {
        KeyReport r;
        r.modifiers = mods;
        r.reserved = 0;
        memcpy(r.keys, keys, 6);
        usbKeyboard.sendReport(&r);
        return;
    }
    if (!bleStarted || !bleConnected) return;
    uint8_t r[8] = { mods, 0, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5] };
    bleKeyIn->setValue(r, sizeof(r));
    bleKeyIn->notify();
}

void hidRelease() {
    static const uint8_t none[6] = { 0, 0, 0, 0, 0, 0 };
    hidKeyboard(0, none);
    hidMouse(0, 0, 0, 0);
}

bool hidTakeAuthEvent() {
    if (!authEvent) return false;
    authEvent = false;
    return true;
}

static int bondList(esp_ble_bond_dev_t** out) {
    *out = nullptr;
    int n = esp_ble_get_bond_device_num();
    if (n <= 0) return 0;
    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * n);
    if (!list) return 0;
    esp_ble_get_bond_device_list(&n, list);
    *out = list;
    return n;
}

void hidSyncBonds(int slot) {
    if (!bleStarted || !sdReady) return;
    esp_ble_bond_dev_t* list;
    int n = bondList(&list);
    BondRec recs[MAX_BONDS];
    int m = bondsLoad(recs, MAX_BONDS);
    int k = 0;
    for (int i = 0; i < m; i++) {
        bool live = false;
        for (int j = 0; j < n && !live; j++) live = !memcmp(recs[i].addr, list[j].bd_addr, 6);
        if (live) recs[k++] = recs[i];
    }
    for (int j = 0; j < n && k < MAX_BONDS; j++) {
        bool known = false;
        for (int i = 0; i < k && !known; i++) known = !memcmp(recs[i].addr, list[j].bd_addr, 6);
        if (!known) {
            recs[k].slot = (uint8_t)slot;
            memcpy(recs[k].addr, list[j].bd_addr, 6);
            k++;
        }
    }
    bondsSave(recs, k);
    free(list);
}

void hidClearSlot(int slot) {
    if (!bleStarted) return;
    if (!sdReady) {
        esp_ble_bond_dev_t* list;
        int n = bondList(&list);
        for (int j = 0; j < n; j++) esp_ble_remove_bond_device(list[j].bd_addr);
        free(list);
        for (int n2 = 1; n2 <= 3; n2++) bumpGen(n2);
        return;
    }
    hidSyncBonds(slot);
    BondRec recs[MAX_BONDS];
    int m = bondsLoad(recs, MAX_BONDS);
    int k = 0;
    for (int i = 0; i < m; i++) {
        if (recs[i].slot == slot) esp_ble_remove_bond_device(recs[i].addr);
        else recs[k++] = recs[i];
    }
    bondsSave(recs, k);
    bumpGen(slot);
}
