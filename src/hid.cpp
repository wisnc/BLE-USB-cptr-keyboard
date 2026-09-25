#include "hid.h"
#include "config.h"
#include "debug.h"
#include <M5Cardputer.h>
#include <USB.h>
#include <USBHIDMouse.h>
#include <USBHIDKeyboard.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_mac.h>
#include <SD.h>
#include <string.h>

extern "C" {
extern struct ble_store_value_sec ble_store_config_our_secs[];
extern int ble_store_config_num_our_secs;
extern struct ble_store_value_sec ble_store_config_peer_secs[];
extern int ble_store_config_num_peer_secs;
extern struct ble_store_value_cccd ble_store_config_cccds[];
extern int ble_store_config_num_cccds;
int ble_store_config_write(int obj_type, const union ble_store_value* val);
int ble_store_config_delete(int obj_type, const union ble_store_key* key);
int ble_hs_misc_restore_irks(void);
bool tud_connected(void);
}

static const int MAX_SECS = CONFIG_BT_NIMBLE_MAX_BONDS;
static const int MAX_CCCDS = CONFIG_BT_NIMBLE_MAX_CCCDS;
static const uint32_t STORE_MAGIC = 0x314E424B;
static const uint16_t APPEARANCE_MOUSE = 0x03C2;

static USBHIDMouse usbMouse;
static USBHIDKeyboard usbKeyboard;
static bool useUsb = false;
static uint8_t usbButtons = 0;

static NimBLEHIDDevice* bleHid = nullptr;
static NimBLECharacteristic* bleMouseIn = nullptr;
static NimBLECharacteristic* bleKeyIn = nullptr;
static bool bleStarted = false;
static volatile bool bleConnected = false;
static int bleSlot = 1;

static volatile bool storeDirty = false;
static volatile uint32_t storeTouched = 0;
static uint32_t lastBattery = 0;

static SlotState slots[3];
static char activeName[40];
static char bleName[24];

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

struct StoreHeader {
    uint32_t magic;
    uint16_t secSize;
    uint16_t cccdSize;
    uint8_t nOur;
    uint8_t nPeer;
    uint8_t nCccd;
    uint8_t pad;
};

static void storePath(char* out, int slot) {
    sprintf(out, "/.keybm/bt/slot%d", slot);
}

static bool peerBonded(const ble_addr_t& a) {
    for (int i = 0; i < ble_store_config_num_peer_secs; i++) {
        const ble_addr_t& b = ble_store_config_peer_secs[i].peer_addr;
        if (a.type == b.type && !memcmp(a.val, b.val, 6)) return true;
    }
    return false;
}

static void addrText(char* out, const ble_addr_t& a) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", a.val[5], a.val[4], a.val[3], a.val[2], a.val[1], a.val[0]);
}

static bool storeSave() {
    if (!sdReady) {
        dbg.saveState = 0;
        return false;
    }
    if (!SD.exists("/.keybm")) SD.mkdir("/.keybm");
    if (!SD.exists("/.keybm/bt")) SD.mkdir("/.keybm/bt");
    char path[32], tmp[36];
    storePath(path, bleSlot);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    StoreHeader h;
    h.magic = STORE_MAGIC;
    h.secSize = sizeof(ble_store_value_sec);
    h.cccdSize = sizeof(ble_store_value_cccd);
    h.nOur = (uint8_t)ble_store_config_num_our_secs;
    h.nPeer = (uint8_t)ble_store_config_num_peer_secs;
    h.nCccd = (uint8_t)ble_store_config_num_cccds;
    h.pad = 0;
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) {
        dbg.saveState = 0;
        dbgLog(true, "save open fail");
        return false;
    }
    size_t want = sizeof(h) + h.nOur * h.secSize + h.nPeer * h.secSize + h.nCccd * h.cccdSize;
    size_t got = f.write((const uint8_t*)&h, sizeof(h));
    got += f.write((const uint8_t*)ble_store_config_our_secs, h.nOur * h.secSize);
    got += f.write((const uint8_t*)ble_store_config_peer_secs, h.nPeer * h.secSize);
    got += f.write((const uint8_t*)ble_store_config_cccds, h.nCccd * h.cccdSize);
    f.close();
    bool ok = got == want;
    if (ok) {
        SD.remove(path);
        ok = SD.rename(tmp, path);
    } else {
        SD.remove(tmp);
    }
    dbg.saveState = ok ? 1 : 0;
    dbgLog(!ok, "save %s o%d p%d c%d", ok ? "ok" : "fail", h.nOur, h.nPeer, h.nCccd);
    return ok;
}

static void storeLoad() {
    dbg.loadState = -1;
    if (!sdReady) return;
    char path[32];
    storePath(path, bleSlot);
    if (!SD.exists(path)) return;
    File f = SD.open(path, FILE_READ);
    if (!f) {
        dbg.loadState = 0;
        return;
    }
    StoreHeader h;
    bool ok = f.read((uint8_t*)&h, sizeof(h)) == (int)sizeof(h) && h.magic == STORE_MAGIC &&
              h.secSize == sizeof(ble_store_value_sec) && h.cccdSize == sizeof(ble_store_value_cccd) &&
              h.nOur <= MAX_SECS && h.nPeer <= MAX_SECS && h.nCccd <= MAX_CCCDS;
    if (ok) {
        ok = f.read((uint8_t*)ble_store_config_our_secs, h.nOur * h.secSize) == (int)(h.nOur * h.secSize) &&
             f.read((uint8_t*)ble_store_config_peer_secs, h.nPeer * h.secSize) == (int)(h.nPeer * h.secSize) &&
             f.read((uint8_t*)ble_store_config_cccds, h.nCccd * h.cccdSize) == (int)(h.nCccd * h.cccdSize);
    }
    f.close();
    if (!ok) {
        ble_store_config_num_our_secs = 0;
        ble_store_config_num_peer_secs = 0;
        ble_store_config_num_cccds = 0;
        dbg.loadState = 0;
        dbgLog(true, "load bad file");
        return;
    }
    ble_store_config_num_our_secs = h.nOur;
    ble_store_config_num_peer_secs = h.nPeer;
    ble_store_config_num_cccds = h.nCccd;
    ble_hs_misc_restore_irks();
    dbg.loadState = 1;
    dbgLog(false, "load o%d p%d c%d", h.nOur, h.nPeer, h.nCccd);
}

static int storeWrite(int type, const union ble_store_value* val) {
    int rc = ble_store_config_write(type, val);
    if (rc == BLE_HS_ESTORE_CAP) return rc;
    storeTouched = millis();
    storeDirty = true;
    dbgLog(false, "store wr t%d", type);
    return 0;
}

static int storeDelete(int type, const union ble_store_key* key) {
    int rc = ble_store_config_delete(type, key);
    if (rc == BLE_HS_ENOENT) return rc;
    storeTouched = millis();
    storeDirty = true;
    dbgLog(false, "store del t%d", type);
    return 0;
}

static int gapEvent(struct ble_gap_event* e, void*) {
    ble_gap_conn_desc d;
    char a[20];
    switch (e->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (e->connect.status != 0) {
                dbgLog(true, "conn fail s%d", e->connect.status);
                break;
            }
            if (ble_gap_conn_find(e->connect.conn_handle, &d) == 0) {
                addrText(a, d.peer_ota_addr);
                strncpy(dbg.peer, a, sizeof(dbg.peer) - 1);
                const char* kind = peerBonded(d.peer_id_addr) ? "bonded" : "new";
                if (kind[0] == 'n' && d.peer_ota_addr.type == BLE_ADDR_RANDOM && (d.peer_ota_addr.val[5] & 0xC0) == 0x40) kind = "rpa";
                dbgLog(false, "conn %s %s", a, kind);
            }
            dbg.connected = true;
            dbg.encrypted = false;
            break;
        case BLE_GAP_EVENT_DISCONNECT: {
            int r = e->disconnect.reason;
            if (r >= BLE_HS_ERR_HCI_BASE) r -= BLE_HS_ERR_HCI_BASE;
            dbg.connected = false;
            dbg.encrypted = false;
            dbgLog(true, "disc r%02X", r);
            break;
        }
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (e->enc_change.status == 0) {
                dbg.encrypted = true;
                bool bonded = ble_gap_conn_find(e->enc_change.conn_handle, &d) == 0 && d.sec_state.bonded;
                dbgLog(false, "enc ok b%d", bonded ? 1 : 0);
            } else {
                dbgLog(true, "enc fail %X", e->enc_change.status);
            }
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            dbgLog(false, "sub h%d n%d r%d", e->subscribe.attr_handle, e->subscribe.cur_notify, e->subscribe.reason);
            break;
        case BLE_GAP_EVENT_CONN_UPDATE:
            if (ble_gap_conn_find(e->conn_update.conn_handle, &d) == 0) {
                dbgLog(false, "prm i%d l%d t%d", d.conn_itvl, d.conn_latency, d.supervision_timeout);
            }
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            dbgLog(true, "repeat pairing");
            break;
        case BLE_GAP_EVENT_MTU:
            dbgLog(false, "mtu %d", e->mtu.value);
            break;
        default:
            break;
    }
    return 0;
}

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, ble_gap_conn_desc*) override {
        bleConnected = true;
    }
    void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) override {
        bleConnected = false;
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

static void newSuffix(char* out) {
    static const char AB[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 4; i++) out[i] = AB[esp_random() % 32];
    out[4] = 0;
}

static void slotName(char* out, int len, int slot) {
    const SlotState& st = slots[slot - 1];
    if (st.gen == 0) snprintf(out, len, "%s %d", bleName, slot);
    else snprintf(out, len, "%s %d - %s", bleName, slot, st.suffix);
}

static void applySlotAddress(int slot) {
    uint8_t gen = slots[slot - 1].gen;
    if (slot <= 1 && gen == 0) return;
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return;
    mac[0] = (uint8_t)((mac[0] | 0x02) & 0xFE);
    mac[4] ^= (uint8_t)(slot << 4);
    mac[3] ^= gen;
    esp_base_mac_addr_set(mac);
}

static uint8_t batteryLevel() {
    int32_t b = M5.Power.getBatteryLevel();
    if (b < 0) return 100;
    if (b > 100) return 100;
    return (uint8_t)b;
}

void hidStartBle(const char* name, int slot) {
    bleSlot = slot;
    strncpy(bleName, name, sizeof(bleName) - 1);
    bleName[sizeof(bleName) - 1] = 0;
    slotsLoad(slots);
    for (int i = 0; i < 3; i++) {
        if (slots[i].gen && !slots[i].suffix[0]) newSuffix(slots[i].suffix);
    }
    applySlotAddress(slot);
    slotName(activeName, sizeof(activeName), slot);
    dbg.slot = slot;
    dbg.gen = slots[slot - 1].gen;
    strncpy(dbg.name, activeName, sizeof(dbg.name) - 1);

    NimBLEDevice::init(activeName);
    ble_hs_cfg.store_write_cb = storeWrite;
    ble_hs_cfg.store_delete_cb = storeDelete;
    static struct ble_gap_event_listener listener;
    ble_gap_event_listener_register(&listener, gapEvent, nullptr);

    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCb());
    server->advertiseOnDisconnect(true);

    bleHid = new NimBLEHIDDevice(server);
    bleMouseIn = bleHid->inputReport(1);
    bleKeyIn = bleHid->inputReport(2);
    bleHid->outputReport(2);
    bleHid->manufacturer()->setValue("M5Stack");
    bleHid->pnp(0x02, 0x1234, 0x5678, 0x0100);
    bleHid->hidInfo(0x00, 0x01);
    bleHid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    bleHid->setBatteryLevel(batteryLevel());
    bleHid->startServices();

    storeLoad();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(APPEARANCE_MOUSE);
    adv->addServiceUUID(bleHid->hidService()->getUUID());
    adv->setScanResponse(true);
    adv->start();
    bleStarted = true;
    lastBattery = millis();

    std::string addr = NimBLEDevice::getAddress().toString();
    slotsSave(slots, slot, addr.c_str());
    strncpy(dbg.addr, addr.c_str(), sizeof(dbg.addr) - 1);
    dbgLog(false, "ble up bonds%d", NimBLEDevice::getNumBonds());
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

bool hidClearSlot(int slot, char* newName, int len) {
    if (newName && len) newName[0] = 0;
    if (!bleStarted) return false;
    NimBLEDevice::deleteAllBonds();
    storeDirty = false;
    if (!sdReady) return false;
    char path[32];
    storePath(path, slot);
    SD.remove(path);
    SlotState& st = slots[slot - 1];
    st.gen = (uint8_t)(st.gen + 1);
    if (st.gen == 0) st.gen = 1;
    newSuffix(st.suffix);
    if (!slotsSave(slots, slot, nullptr)) return false;
    if (newName && len) slotName(newName, len, slot);
    return true;
}

void hidPoll(uint32_t now) {
    if (!bleStarted) return;
    if (storeDirty && now - storeTouched >= 300) {
        storeDirty = false;
        storeSave();
    }
    if (now - lastBattery >= 60000) {
        lastBattery = now;
        bleHid->setBatteryLevel(batteryLevel());
        bleHid->batteryLevel()->notify();
    }
}

void hidFlush() {
    if (bleStarted && storeDirty) {
        storeDirty = false;
        storeSave();
    }
}

void hidStoreInfo(int& our, int& peer, int& cccd, char* firstBond) {
    our = ble_store_config_num_our_secs;
    peer = ble_store_config_num_peer_secs;
    cccd = ble_store_config_num_cccds;
    if (firstBond) {
        if (peer > 0) addrText(firstBond, ble_store_config_peer_secs[0].peer_addr);
        else strcpy(firstBond, "-");
    }
}
