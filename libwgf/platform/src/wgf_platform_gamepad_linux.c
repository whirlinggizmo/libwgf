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
#include "wgf_core_format_priv.h"

/* Gamepads on Linux, through evdev: the /dev/input/event* devices that have gamepad
 * buttons, opened non-blocking, and checked for new ones every couple of seconds. A
 * desktop session can read and write its own joysticks (logind grants it); without
 * that, no pad is found. A device is opened for writing where it can be, else for
 * reading alone: rumble (force feedback's FF_RUMBLE, an effect uploaded once and played
 * again with each rumble's motors and length) needs writing, so a pad opened read-only
 * reads as one without motors. A pad whose GUID (SDL's, from its bus, vendor, product, and
 * version) a mapping has (wgf_gamepad_add_mappings) is read raw, in SDL's order, through it;
 * the rest by the codes below. Cribbed from wgrender's wgr_gamepad. */

#define RESCAN_SECONDS 2.0
#define LONG_BITS (sizeof(long) * CHAR_BIT)
#define HAS_BIT(bits, n) (((bits)[(n) / LONG_BITS] >> ((n) % LONG_BITS)) & 1UL)

typedef struct device_t {
    int fd; /* -1: the slot is free */
    char path[288];
    bool xpad;       /* its X and Y buttons use BTN_NORTH's and BTN_WEST's codes */
    bool right_on_z; /* no RX and RY: the right stick is on Z and RZ (generic HID pads) */
    bool rumble;     /* opened for writing, with FF_RUMBLE */
    int effect;      /* the rumble effect's id, once uploaded; -1 before */
    struct input_absinfo abs[ABS_MAX + 1];
    /* read raw, for a mapping: each code's place in SDL's order, -1 for none */
    signed char key_index[KEY_MAX + 1];
    signed char abs_index[ABS_MAX + 1];
    signed char hat_index[ABS_HAT3Y + 1]; /* by its X code */
    wgf_platform_priv_gamepad_raw_t raw;
    const wgf_platform_priv_gamepad_mapping_t *mapping; /* NULL: read by the codes */
    bool looked_up;
    unsigned generation; /* the mappings' when it was looked up */
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

/* The raw state's places, SDL's order (its linux joystick's): buttons from BTN_JOYSTICK up,
 * then those below it; axes from ABS_X up, the hats left out; hats from ABS_HAT0X, a pair each. */
static void raw_layout(device_t *dev, const unsigned long *key_bits, const unsigned long *abs_bits)
{
    wgf_platform_priv_gamepad_raw_t *raw = &dev->raw;
    int code;
    memset(dev->key_index, -1, sizeof(dev->key_index));
    memset(dev->abs_index, -1, sizeof(dev->abs_index));
    memset(dev->hat_index, -1, sizeof(dev->hat_index));
    for (code = BTN_JOYSTICK; code < KEY_MAX && raw->button_count < WGF_PLATFORM_PRIV_GAMEPAD_RAW; code++) {
        if (HAS_BIT(key_bits, code)) dev->key_index[code] = (signed char)raw->button_count++;
    }
    for (code = 0; code < BTN_JOYSTICK && raw->button_count < WGF_PLATFORM_PRIV_GAMEPAD_RAW; code++) {
        if (HAS_BIT(key_bits, code)) dev->key_index[code] = (signed char)raw->button_count++;
    }
    for (code = 0; code < ABS_MAX && raw->axis_count < WGF_PLATFORM_PRIV_GAMEPAD_RAW; code++) {
        if (code >= ABS_HAT0X && code <= ABS_HAT3Y) continue;
        if (HAS_BIT(abs_bits, code)) dev->abs_index[code] = (signed char)raw->axis_count++;
    }
    for (code = ABS_HAT0X; code <= ABS_HAT3Y && raw->hat_count < WGF_PLATFORM_PRIV_GAMEPAD_HATS; code += 2) {
        if (HAS_BIT(abs_bits, code) || HAS_BIT(abs_bits, code + 1))
            dev->hat_index[code] = (signed char)raw->hat_count++;
    }
}

/* A key or an axis into the raw state. */
static void raw_key(device_t *dev, int code, int value)
{
    if (code >= 0 && code <= KEY_MAX && dev->key_index[code] >= 0)
        dev->raw.buttons[(int)dev->key_index[code]] = value != 0;
}

static void raw_abs(device_t *dev, int code, int value)
{
    if (code < 0 || code > ABS_MAX) return;
    if (code >= ABS_HAT0X && code <= ABS_HAT3Y) {
        const int x_code = code - ((code - ABS_HAT0X) & 1), hat = dev->hat_index[x_code];
        int *bits;
        if (hat < 0) return;
        bits = &dev->raw.hats[hat];
        if (code == x_code) *bits = (*bits & ~(2 | 8)) | (value > 0 ? 2 : 0) | (value < 0 ? 8 : 0);
        else *bits = (*bits & ~(1 | 4)) | (value < 0 ? 1 : 0) | (value > 0 ? 4 : 0);
    } else if (dev->abs_index[code] >= 0) {
        dev->raw.axes[(int)dev->abs_index[code]] = abs_value(dev, code, value, false);
    }
}

/* Its mapping looked up again where the mappings have changed. */
static void look_up(device_t *dev, wgf_platform_priv_gamepad_t *pad)
{
    if (dev->looked_up && dev->generation == wgf_platform_priv_gamepad_mapping_generation()) return;
    dev->looked_up = true;
    dev->generation = wgf_platform_priv_gamepad_mapping_generation();
    dev->mapping = wgf_platform_priv_gamepad_find_mapping(pad->guid);
    pad->mapped = dev->mapping != NULL;
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
    wgf_core_priv_format(link, sizeof(link), "/sys/class/input/%s/device/device/driver", event_name);
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
    unsigned long ff_bits[(FF_MAX + LONG_BITS) / LONG_BITS] = {0};
    bool writable = true;
    unsigned char guid_bytes[16] = {0};
    char path[288];
    int slot = -1, fd, i, code;
    device_t *dev;
    wgf_platform_priv_gamepad_t *pad;

    wgf_core_priv_format(path, sizeof(path), "/dev/input/%s", event_name);
    if (is_open(path)) return;
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS && slot < 0; i++) {
        if (devices[i].fd < 0) slot = i;
    }
    if (slot < 0) return;
    fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        writable = false;
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (fd < 0) return;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0 || !HAS_BIT(key_bits, BTN_GAMEPAD)) {
        close(fd); /* not a gamepad: a keyboard, a mouse, ... */
        return;
    }
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
    {
        struct input_id id;
        memset(&id, 0, sizeof(id));
        /* SDL's: bus, 0, vendor, 0, product, 0, version, 0 */
        if (ioctl(fd, EVIOCGID, &id) >= 0 && id.vendor != 0 && id.product != 0) {
            const unsigned short words[8] = {id.bustype, 0, id.vendor, 0, id.product, 0, id.version, 0};
            for (i = 0; i < 8; i++) {
                guid_bytes[i * 2] = (unsigned char)(words[i] & 0xFF);
                guid_bytes[i * 2 + 1] = (unsigned char)(words[i] >> 8);
            }
        }
    }

    dev = &devices[slot];
    pad = &pads[slot];
    memset(dev, 0, sizeof(*dev));
    memset(pad, 0, sizeof(*pad));
    dev->fd = fd;
    wgf_core_priv_format(dev->path, sizeof(dev->path), "%s", path);
    dev->xpad = driver_is(event_name, "xpad");
    dev->right_on_z = !HAS_BIT(abs_bits, ABS_RX) && HAS_BIT(abs_bits, ABS_Z) && HAS_BIT(abs_bits, ABS_RZ);
    dev->rumble = writable && ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) >= 0 && HAS_BIT(ff_bits, FF_RUMBLE);
    dev->effect = -1;
    pad->rumble = dev->rumble;
    memcpy(pad->guid, guid_bytes, sizeof(pad->guid));
    raw_layout(dev, key_bits, abs_bits);
    if (ioctl(fd, EVIOCGNAME(sizeof(pad->name)), pad->name) < 0) wgf_core_priv_format(pad->name, sizeof(pad->name),
                                                                                      "gamepad");
    pad->name[WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE - 1] = '\0';
    for (code = 0; code <= ABS_MAX; code++) {
        if (HAS_BIT(abs_bits, code) && ioctl(fd, EVIOCGABS(code), &dev->abs[code]) == 0) {
            if (code <= ABS_HAT0Y) apply_abs(dev, pad, code, dev->abs[code].value);
            raw_abs(dev, code, dev->abs[code].value);
        }
    }
    if (ioctl(fd, EVIOCGKEY(sizeof(keys_down)), keys_down) >= 0) {
        for (code = 0; code < KEY_MAX; code++) {
            const int button = code >= BTN_MISC && code <= BTN_DPAD_RIGHT ? button_of(dev, code) : -1;
            if (!HAS_BIT(keys_down, code)) continue;
            if (button >= 0) pad->buttons[button] = true;
            raw_key(dev, code, 1);
        }
    }
    pad->connected = true;
    look_up(dev, pad);
    if (dev->mapping != NULL) wgf_platform_priv_gamepad_apply_mapping(dev->mapping, &dev->raw, pad);
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
                    raw_key(dev, event->code, event->value);
                } else if (event->type == EV_ABS) {
                    if (event->code <= ABS_HAT0Y) apply_abs(dev, &pads[slot], event->code, event->value);
                    raw_abs(dev, event->code, event->value);
                }
            }
            if (n < (ssize_t)sizeof(events)) break;
        }
        if (dev->fd < 0) continue; /* unplugged as it was read */
        look_up(dev, &pads[slot]);
        if (dev->mapping != NULL) wgf_platform_priv_gamepad_apply_mapping(dev->mapping, &dev->raw, &pads[slot]);
    }
}

/* An effect played (1) or stopped (0). */
static void play_effect(const device_t *dev, int value)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = EV_FF;
    event.code = (unsigned short)dev->effect;
    event.value = value;
    if (write(dev->fd, &event, sizeof(event)) != (ssize_t)sizeof(event)) {
        /* a pad unplugged meanwhile: the next poll's read closes it */
    }
}

void wgf_platform_priv_gamepad_platform_rumble(int pad, float low, float high, float seconds)
{
    device_t *dev;
    struct ff_effect effect;
    if (pad < 0 || pad >= WGF_PLATFORM_PRIV_GAMEPADS) return;
    dev = &devices[pad];
    if (dev->fd < 0 || !dev->rumble) return;
    if (seconds <= 0.0f) {
        if (dev->effect >= 0) play_effect(dev, 0);
        return;
    }
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE;
    effect.id = (short)dev->effect; /* -1 uploads a new one; its own id changes it */
    effect.u.rumble.strong_magnitude = (unsigned short)(low * 65535.0f + 0.5f);
    effect.u.rumble.weak_magnitude = (unsigned short)(high * 65535.0f + 0.5f);
    effect.replay.length = (unsigned short)(seconds * 1000.0f + 0.5f);
    if (ioctl(dev->fd, EVIOCSFF, &effect) < 0) return;
    dev->effect = effect.id;
    play_effect(dev, 1);
}
