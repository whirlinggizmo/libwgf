#include "clay.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_ui_scroll_priv.h"

/* The Clay UI's scrolling, its frame's start (wgf_ui_scroll_priv.h): reached through the
 * hook the first scroll area or list installs, so a UI without one links none of it, nor
 * Clay's update of its scroll containers. */

static void frame(wgf_ui_priv_scroll_t *scroll, bool pressed, bool down)
{
    const Clay_ElementIdArray over = Clay_GetPointerOverIds();
    const wgf_vec2_t wheel = wgf_mouse_get_wheel();
    const bool shift = wgf_keyboard_is_down(WGF_KEY_LEFT_SHIFT) || wgf_keyboard_is_down(WGF_KEY_RIGHT_SHIFT);
    const Clay_Vector2 none = {0.0f, 0.0f};
    wgf_ui_priv_area_t *swap = scroll->before;
    const int capacity = scroll->before_capacity;
    int32_t i;
    int j;
    Clay_UpdateScrollContainers(false, none, 0.0f);
    scroll->before = scroll->areas; /* last frame's: what the pointer and the layout saw */
    scroll->before_count = scroll->area_count;
    scroll->before_capacity = scroll->area_capacity;
    scroll->areas = swap;
    scroll->area_capacity = capacity;
    scroll->area_count = 0;
    scroll->target = 0;
    for (i = over.length - 1; i >= 0 && scroll->target == 0; i--) { /* children after their parents */
        for (j = 0; j < scroll->before_count; j++) {
            if (scroll->before[j].element == over.internalArray[i].id) scroll->target = scroll->before[j].window;
        }
    }
    scroll->wheel[0] = shift ? wheel.y : wheel.x;
    scroll->wheel[1] = shift ? 0.0f : wheel.y;
    if (scroll->wheel[0] != 0.0f || scroll->wheel[1] != 0.0f) scroll->flung = 0;
    if (pressed) {
        scroll->held = scroll->target;
        scroll->started = scroll->dragging = scroll->press_drags = false;
        scroll->bar = -1;
        scroll->flung = 0;
    } else if (!down && scroll->held != 0) {
        if (scroll->dragging) scroll->flung = scroll->held;
        scroll->held = 0;
        scroll->dragging = false;
    }
}

void wgf_ui_priv_install_scrolling(void)
{
    wgf_ui_priv_set_scroll_frame(frame);
}
