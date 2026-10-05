#define _DEFAULT_SOURCE /* readlink, and the linux/input.h ioctls with them */

#include "wgf_platform_gamepad_priv.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "wgf_gamepad.h"
#include "wgf_time.h"

/* Gamepads on Linux, through evdev: the /dev/input/event* devices that have gamepad
 * buttons, opened read-only and non-blocking, and checked for new ones every couple
 * of seconds. A desktop session can read its own joysticks (logind grants it); without
 * that, no pad is found. Cribbed from wgrender's wgr_gamepad. */

#define RESCAN_SECONDS 2.0
#define LONG_BITS (sizeof(long) * CHAR_BIT)
#define HAS_BIT(bits, n) (((bits)[(n) / LONG_BITS] >> ((n) % LONG_BITS)) & 1UL)

typedef struct device_t {
    int fd; /* -1: the slot is free */
    char path[288];
    bool xpad;       /* its X and Y buttons use BTN_NORTH's and BTN_WEST's codes */
    bool right_on_z; /* no RX and RY: the right stick is on Z and RZ (generic HID pads) */
    struct input_absinfo abs[ABS_HAT0Y + 1];
} device_t;

static device_t devices[WGF_PLATFORM_PRIV_GAMEPADS];
static double next_scan;

static float abs_value(const device_t *dev, int code, int value, bool trigger)
{
    const struct input_absinfo *info = &dev->abs[code];
    const float range = (float)(info->maximum - info->minimum);
    float t;
    if (range <= 0.0f) return 0.0f;
    t = (float)(value - info->minimum) / range;
    return trigger ? t : t * 2.0f - 1.0f;
}

static int button_of(const device_t *dev, int code)
{
    switch (code) {
        case BTN_SOUTH: return WGF_GAMEPAD_BUTTON_SOUTH;
        case BTN_EAST: return WGF_GAMEPAD_BUTTON_EAST;
        case BTN_NORTH: return dev->xpad ? WGF_GAMEPAD_BUTTON_WEST : WGF_GAMEPAD_BUTTON_NORTH; /* BTN_X */
        case BTN_WEST: return dev->xpad ? WGF_GAMEPAD_BUTTON_NORTH : WGF_GAMEPAD_BUTTON_WEST;  /* BTN_Y */
        case BTN_TL: return WGF_GAMEPAD_BUTTON_LEFT_BUMPER;
        case BTN_TR: return WGF_GAMEPAD_BUTTON_RIGHT_BUMPER;
        case BTN_TL2: return WGF_GAMEPAD_BUTTON_LEFT_TRIGGER;
        case BTN_TR2: return WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER;
        case BTN_SELECT: return WGF_GAMEPAD_BUTTON_BACK;
        case BTN_START: return WGF_GAMEPAD_BUTTON_START;
        case BTN_MODE: return WGF_GAMEPAD_BUTTON_GUIDE;
        case BTN_THUMBL: return WGF_GAMEPAD_BUTTON_LEFT_STICK;
        case BTN_THUMBR: return WGF_GAMEPAD_BUTTON_RIGHT_STICK;
        case BTN_DPAD_UP: return WGF_GAMEPAD_BUTTON_DPAD_UP;
        case BTN_DPAD_DOWN: return WGF_GAMEPAD_BUTTON_DPAD_DOWN;
        case BTN_DPAD_LEFT: return WGF_GAMEPAD_BUTTON_DPAD_LEFT;
        case BTN_DPAD_RIGHT: return WGF_GAMEPAD_BUTTON_DPAD_RIGHT;
        default: return -1;
    }
}

static void apply_abs(const device_t *dev, wgf_platform_priv_gamepad_t *pad, int code, int value)
{
    switch (code) {
        case ABS_X: pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X] = abs_value(dev, code, value, false); break;
        case ABS_Y: pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y] = abs_value(dev, code, value, false); break;
        case ABS_RX: pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X] = abs_value(dev, code, value, false); break;
        case ABS_RY: pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_Y] = abs_value(dev, code, value, false); break;
        case ABS_Z:
            if (dev->right_on_z) {
                pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X] = abs_value(dev, code, value, false);
            } else {
                pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER] = abs_value(dev, code, value, true);
            }
            break;
        case ABS_RZ:
            if (dev->right_on_z) {
                pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_Y] = abs_value(dev, code, value, false);
            } else {
                pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER] = abs_value(dev, code, value, true);
            }
            break;
        case ABS_BRAKE: pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER] = abs_value(dev, code, value, true); break;
        case ABS_GAS: pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER] = abs_value(dev, code, value, true); break;
        case ABS_HAT0X:
            pad->buttons[WGF_GAMEPAD_BUTTON_DPAD_LEFT] = value < 0;
            pad->buttons[WGF_GAMEPAD_BUTTON_DPAD_RIGHT] = value > 0;
            break;
        case ABS_HAT0Y:
            pad->buttons[WGF_GAMEPAD_BUTTON_DPAD_UP] = value < 0;
            pad->buttons[WGF_GAMEPAD_BUTTON_DPAD_DOWN] = value > 0;
            break;
        default: break;
    }
}

static bool is_open(const char *path)
{
    int i;
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS; i++) {
        if (devices[i].fd >= 0 && strcmp(devices[i].path, path) == 0) return true;
    }
    return false;
}

/* The kernel driver behind /dev/input/eventN, from sysfs: "xpad" for Xbox pads. */
static bool driver_is(const char *event_name, const char *driver)
{
    char link[320], target[256];
    const char *base;
    ssize_t n;
    snprintf(link, sizeof(link), "/sys/class/input/%s/device/device/driver", event_name);
    n = readlink(link, target, sizeof(target) - 1);
    if (n <= 0) return false;
    target[n] = '\0';
    base = strrchr(target, '/');
    return strcmp(base != NULL ? base + 1 : target, driver) == 0;
}

/* Open a device if it's a gamepad and a slot is free; its state now goes into the
 * slot, so buttons held and sticks already off-center count from the start. */
static void try_open(const char *event_name, wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS])
{
    unsigned long key_bits[(KEY_MAX + LONG_BITS) / LONG_BITS] = {0};
    unsigned long abs_bits[(ABS_MAX + LONG_BITS) / LONG_BITS] = {0};
    unsigned long keys_down[(KEY_MAX + LONG_BITS) / LONG_BITS] = {0};
    char path[288];
    int slot = -1, fd, i, code;
    device_t *dev;
    wgf_platform_priv_gamepad_t *pad;

    snprintf(path, sizeof(path), "/dev/input/%s", event_name);
    if (is_open(path)) return;
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS && slot < 0; i++) {
        if (devices[i].fd < 0) slot = i;
    }
    if (slot < 0) return;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0 || !HAS_BIT(key_bits, BTN_GAMEPAD)) {
        close(fd); /* not a gamepad: a keyboard, a mouse, ... */
        return;
    }
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);

    dev = &devices[slot];
    pad = &pads[slot];
    memset(dev, 0, sizeof(*dev));
    memset(pad, 0, sizeof(*pad));
    dev->fd = fd;
    snprintf(dev->path, sizeof(dev->path), "%s", path);
    dev->xpad = driver_is(event_name, "xpad");
    dev->right_on_z = !HAS_BIT(abs_bits, ABS_RX) && HAS_BIT(abs_bits, ABS_Z) && HAS_BIT(abs_bits, ABS_RZ);
    if (ioctl(fd, EVIOCGNAME(sizeof(pad->name)), pad->name) < 0) snprintf(pad->name, sizeof(pad->name), "gamepad");
    pad->name[WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE - 1] = '\0';
    for (code = 0; code <= ABS_HAT0Y; code++) {
        if (HAS_BIT(abs_bits, code) && ioctl(fd, EVIOCGABS(code), &dev->abs[code]) == 0) {
            apply_abs(dev, pad, code, dev->abs[code].value);
        }
    }
    if (ioctl(fd, EVIOCGKEY(sizeof(keys_down)), keys_down) >= 0) {
        for (code = BTN_MISC; code <= BTN_DPAD_RIGHT; code++) {
            const int button = button_of(dev, code);
            if (button >= 0 && HAS_BIT(keys_down, code)) pad->buttons[button] = true;
        }
    }
    pad->connected = true;
}

static void rescan(wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS])
{
    DIR *dir = opendir("/dev/input");
    struct dirent *entry;
    if (dir == NULL) return;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) == 0) try_open(entry->d_name, pads);
    }
    closedir(dir);
}

static void close_slot(int slot, wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS])
{
    if (devices[slot].fd >= 0) close(devices[slot].fd);
    devices[slot].fd = -1;
    memset(&pads[slot], 0, sizeof(pads[slot]));
}

void wgf_platform_priv_gamepad_platform_open(void)
{
    int i;
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS; i++) devices[i].fd = -1;
    next_scan = 0.0; /* the first poll scans */
}

void wgf_platform_priv_gamepad_platform_close(void)
{
    int i;
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS; i++) {
        if (devices[i].fd >= 0) close(devices[i].fd);
        devices[i].fd = -1;
    }
}

void wgf_platform_priv_gamepad_platform_poll(wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS])
{
    const double now = wgf_time_get_seconds();
    int slot;
    if (now >= next_scan) {
        rescan(pads);
        next_scan = now + RESCAN_SECONDS;
    }
    for (slot = 0; slot < WGF_PLATFORM_PRIV_GAMEPADS; slot++) {
        device_t *dev = &devices[slot];
        struct input_event events[64];
        if (dev->fd < 0) continue;
        for (;;) {
            const ssize_t n = read(dev->fd, events, sizeof(events));
            int e;
            if (n < 0) {
                if (errno != EAGAIN && errno != EINTR) close_slot(slot, pads); /* ENODEV: unplugged */
                break;
            }
            for (e = 0; e < (int)(n / (ssize_t)sizeof(events[0])); e++) {
                const struct input_event *event = &events[e];
                if (event->type == EV_KEY) {
                    const int button = button_of(dev, event->code);
                    if (button >= 0) pads[slot].buttons[button] = event->value != 0;
                } else if (event->type == EV_ABS && event->code <= ABS_HAT0Y) {
                    apply_abs(dev, &pads[slot], event->code, event->value);
                }
            }
            if (n < (ssize_t)sizeof(events)) break;
        }
    }
}
