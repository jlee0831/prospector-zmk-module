#pragma once
#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

#define KH_RING_BUFFER_SIZE 64

typedef enum {
    KH_KEY_PRESS,
    KH_KEY_RELEASE,
    KH_LAYER_CHANGE,
} kh_event_type_t;

typedef struct {
    kh_event_type_t type;
    uint8_t  layer;        /* active layer at time of event */
    uint32_t position;     /* physical key position (0xFFFFFFFF = unknown) */
    uint32_t keycode;      /* resolved HID keycode (0 = none yet) */
    uint8_t  mods;         /* explicit modifiers at time of press */
    uint8_t  new_layer;    /* layer that changed (KH_LAYER_CHANGE only) */
    bool     layer_active; /* true=activated, false=deactivated (KH_LAYER_CHANGE only) */
    uint32_t timestamp_ms; /* k_uptime_get() truncated to 32-bit */
} kh_entry_t;

/* Ring buffer API — callable from any thread (mutex-protected) */
void     kh_push(const kh_entry_t *entry);
uint8_t  kh_count(void);
/* index 0 = newest entry, index 1 = second newest, etc. */
bool     kh_get(uint8_t newest_offset, kh_entry_t *out);
/* Attach keycode to most recent unresolved KH_KEY_PRESS.
 * If none exists, stores as standalone KH_KEY_PRESS with position=0xFFFFFFFF. */
void     kh_attach_keycode(uint32_t keycode, uint8_t mods);

/* Recording pause — called from behavior/display thread */
void kh_set_recording(bool enabled);
bool kh_is_recording(void);

/* Screen state — accessed from both threads; atomic */
void kh_set_active(bool active);
bool kh_is_active(void);

/* Scroll offset — only accessed from display thread */
void    kh_set_scroll_offset(uint8_t offset);
uint8_t kh_get_scroll_offset(void);

/* Listener init — called once at startup */
void kh_listener_init(void);

/* Screen API — called from display thread only */
lv_obj_t *kh_screen_create(void);
void      kh_screen_rebuild(void);

/* Screen references — set by custom_status_screen.c at init */
void      kh_set_screens(lv_obj_t *normal, lv_obj_t *history);
lv_obj_t *kh_get_screen(void);
lv_obj_t *kh_get_normal_screen(void);

/* Behavior API — called from ZMK main thread, dispatches to display thread */
void kh_cmd_toggle(void);
void kh_cmd_scroll_up(void);
void kh_cmd_scroll_down(void);
