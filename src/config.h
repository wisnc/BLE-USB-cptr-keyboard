#pragma once
#include <Arduino.h>

enum LinkPref : uint8_t { LINK_AUTO, LINK_BT, LINK_USB };

struct Config {
    uint32_t theme;
    bool imuPose;
    float imuDead;
    float imuMax;
    float imuSpeed;
    int8_t imuAxisX;
    int8_t imuSignX;
    int8_t imuAxisY;
    int8_t imuSignY;
    int moveSpeed;
    int brightness;
    int brightStep;
    int slot;
    LinkPref link;
    int usbWait;
    char name[24];
    int scrollDir;
};

struct BondRec {
    uint8_t slot;
    uint8_t addr[6];
};

static const int MAX_BONDS = 16;

extern Config cfg;
extern bool sdReady;

void configBegin();
bool configSave();
int bondsLoad(BondRec* out, int max);
bool bondsSave(const BondRec* recs, int n);
