#include <zephyr/kernel.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include "key_history.h"

/* ── Ring buffer ─────────────────────────────────────────── */

static kh_entry_t kh_buf[KH_RING_BUFFER_SIZE];
static uint8_t    kh_head = 0; /* next write slot */
static uint8_t    kh_cnt  = 0; /* number of valid entries */
K_MUTEX_DEFINE(kh_mutex);

void kh_push(const kh_entry_t *entry) {
    k_mutex_lock(&kh_mutex, K_FOREVER);
    kh_buf[kh_head] = *entry;
    kh_head = (kh_head + 1) % KH_RING_BUFFER_SIZE;
    if (kh_cnt < KH_RING_BUFFER_SIZE) {
        kh_cnt++;
    }
    k_mutex_unlock(&kh_mutex);
}

uint8_t kh_count(void) {
    k_mutex_lock(&kh_mutex, K_FOREVER);
    uint8_t c = kh_cnt;
    k_mutex_unlock(&kh_mutex);
    return c;
}

bool kh_get(uint8_t newest_offset, kh_entry_t *out) {
    k_mutex_lock(&kh_mutex, K_FOREVER);
    if (newest_offset >= kh_cnt) {
        k_mutex_unlock(&kh_mutex);
        return false;
    }
    /* newest is at (kh_head - 1), offset 0 steps back from there */
    uint8_t idx = (kh_head - 1 - newest_offset + KH_RING_BUFFER_SIZE * 2)
                  % KH_RING_BUFFER_SIZE;
    *out = kh_buf[idx];
    k_mutex_unlock(&kh_mutex);
    return true;
}

void kh_attach_keycode(uint32_t keycode, uint8_t mods) {
    k_mutex_lock(&kh_mutex, K_FOREVER);
    /* Walk backward from newest looking for an unresolved KH_KEY_PRESS */
    for (uint8_t i = 0; i < kh_cnt && i < 4; i++) {
        uint8_t idx = (kh_head - 1 - i + KH_RING_BUFFER_SIZE * 2) % KH_RING_BUFFER_SIZE;
        if (kh_buf[idx].type == KH_KEY_PRESS && kh_buf[idx].keycode == 0) {
            kh_buf[idx].keycode = keycode;
            kh_buf[idx].mods    = mods;
            k_mutex_unlock(&kh_mutex);
            return;
        }
    }
    /* No unresolved press — store standalone */
    k_mutex_unlock(&kh_mutex);
    kh_entry_t e = {
        .type         = KH_KEY_PRESS,
        .layer        = zmk_keymap_highest_layer_active(),
        .position     = 0xFFFFFFFF,
        .keycode      = keycode,
        .mods         = mods,
        .timestamp_ms = (uint32_t)k_uptime_get(),
    };
    kh_push(&e);
}

/* ── Recording / active / scroll flags ──────────────────── */

static atomic_t kh_recording = ATOMIC_INIT(1);
static atomic_t kh_active    = ATOMIC_INIT(0);
static uint8_t  kh_scroll    = 0;

void    kh_set_recording(bool en) { atomic_set(&kh_recording, en ? 1 : 0); }
bool    kh_is_recording(void)     { return atomic_get(&kh_recording) != 0; }
void    kh_set_active(bool a)     { atomic_set(&kh_active, a ? 1 : 0); }
bool    kh_is_active(void)        { return atomic_get(&kh_active) != 0; }
void    kh_set_scroll_offset(uint8_t o) { kh_scroll = o; }
uint8_t kh_get_scroll_offset(void)      { return kh_scroll; }

/* ── Screen references ───────────────────────────────────── */

static lv_obj_t *s_normal_screen  = NULL;
static lv_obj_t *s_history_screen = NULL;

void kh_set_screens(lv_obj_t *normal, lv_obj_t *history) {
    s_normal_screen  = normal;
    s_history_screen = history;
}
lv_obj_t *kh_get_screen(void)        { return s_history_screen; }
lv_obj_t *kh_get_normal_screen(void) { return s_normal_screen; }

/* ── Event listener ─────────────────────────────────────── */

static int kh_event_handler(const zmk_event_t *eh) {
    if (!kh_is_recording()) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *pos =
        as_zmk_position_state_changed(eh);
    if (pos) {
        kh_entry_t e = {
            .type         = pos->state ? KH_KEY_PRESS : KH_KEY_RELEASE,
            .layer        = zmk_keymap_highest_layer_active(),
            .position     = pos->position,
            .keycode      = 0,
            .timestamp_ms = (uint32_t)k_uptime_get(),
        };
        kh_push(&e);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_layer_state_changed *layer =
        as_zmk_layer_state_changed(eh);
    if (layer) {
        kh_entry_t e = {
            .type         = KH_LAYER_CHANGE,
            .layer        = zmk_keymap_highest_layer_active(),
            .new_layer    = layer->layer,
            .layer_active = layer->state,
            .timestamp_ms = (uint32_t)k_uptime_get(),
        };
        kh_push(&e);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_keycode_state_changed *kc =
        as_zmk_keycode_state_changed(eh);
    if (kc && kc->state) {
        kh_attach_keycode(kc->keycode, kc->explicit_modifiers);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_history, kh_event_handler);
ZMK_SUBSCRIPTION(key_history, zmk_position_state_changed);
ZMK_SUBSCRIPTION(key_history, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(key_history, zmk_keycode_state_changed);

void kh_listener_init(void) {
    /* Nothing to do — ZMK_LISTENER/ZMK_SUBSCRIPTION handle registration */
}
