#include "imu.h"
#include "config.h"
#include <M5Cardputer.h>
#include <math.h>

static bool enabled = false;
static bool primed = false;
static float fx = 0, fy = 0, fz = 0;
static float zx = 0, zy = 0;

bool imuSetEnabled(bool on) {
    if (on == enabled) return enabled;
    if (on) {
        if (!M5.Imu.begin(&M5.In_I2C, M5.getBoard())) return false;
        primed = false;
        enabled = true;
    } else {
        M5.Imu.sleep();
        enabled = false;
    }
    return enabled;
}

bool imuEnabled() {
    return enabled;
}

static float axisValue(int axis) {
    return axis == 0 ? fx : (axis == 1 ? fy : fz);
}

static float tiltDeg(float a, float n) {
    float s = a / n;
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return asinf(s) * 57.2957795f;
}

static float shape(float t) {
    float a = fabsf(t);
    if (a <= cfg.imuDead) return 0.0f;
    float span = cfg.imuMax - cfg.imuDead;
    float m = span > 0.5f ? (a - cfg.imuDead) / span : 1.0f;
    if (m > 1.0f) m = 1.0f;
    return t < 0 ? -m : m;
}

bool imuRead(float& nx, float& ny) {
    nx = 0;
    ny = 0;
    if (!enabled) return false;
    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return false;
    if (!primed) {
        fx = ax;
        fy = ay;
        fz = az;
    } else {
        fx += 0.3f * (ax - fx);
        fy += 0.3f * (ay - fy);
        fz += 0.3f * (az - fz);
    }
    float n = sqrtf(fx * fx + fy * fy + fz * fz);
    if (n < 0.2f) return false;
    float tx = cfg.imuSignX * tiltDeg(axisValue(cfg.imuAxisX), n);
    float ty = cfg.imuSignY * tiltDeg(axisValue(cfg.imuAxisY), n);
    if (!primed) {
        primed = true;
        zx = cfg.imuPose ? tx : 0.0f;
        zy = cfg.imuPose ? ty : 0.0f;
    }
    nx = shape(tx - zx);
    ny = shape(ty - zy);
    return true;
}
