#include <zephyr/kernel.h>
#include <lvgl.h>
#include <string.h>
#include <stdio.h>
#include "key_history.h"

#define KH_SCREEN_W     280
#define KH_SCREEN_H     240
#define KH_HEADER_H      32
#define KH_LIST_H       (KH_SCREEN_H - KH_HEADER_H)
#define KH_ROW_H         20
#define KH_VISIBLE_ROWS (KH_LIST_H / KH_ROW_H)  /* 10 */

/* Column x positions */
#define KH_COL_POS_X     8
#define KH_COL_POS_W    36
#define KH_COL_ARR_X    46
#define KH_COL_ARR_W    14
#define KH_COL_KC_X     62
#define KH_COL_KC_W     96
#define KH_COL_BADGE_X 160
#define KH_COL_BADGE_W  68
#define KH_COL_TIME_X  230
#define KH_COL_TIME_W   48

static lv_obj_t *s_screen    = NULL;
static lv_obj_t *s_list_cont = NULL;

/* ── Layer badge colors ───────────────────────────────────── */

typedef struct { uint32_t bg; uint32_t text; } kh_layer_color_t;

static const kh_layer_color_t kh_layer_colors[] = {
    {0x21262d, 0x8b949e}, /* BASE (0) gray   */
    {0x1f2d3d, 0x58a6ff}, /* SYM  (1) blue   */
    {0x2d1f3d, 0xbc8cff}, /* NUM  (2) purple */
    {0x2d1d0d, 0xf0883e}, /* FNC  (3) orange */
};

static kh_layer_color_t kh_layer_color(uint8_t layer) {
    if (layer < ARRAY_SIZE(kh_layer_colors)) {
        return kh_layer_colors[layer];
    }
    return kh_layer_colors[0];
}

/* ── Keycode → short string ───────────────────────────────── */

static const char *kh_kc_str(uint32_t kc, uint8_t mods, char *buf, size_t len) {
    static const struct { uint32_t kc; const char *s; } tbl[] = {
        {0x04,"A"},{0x05,"B"},{0x06,"C"},{0x07,"D"},{0x08,"E"},
        {0x09,"F"},{0x0A,"G"},{0x0B,"H"},{0x0C,"I"},{0x0D,"J"},
        {0x0E,"K"},{0x0F,"L"},{0x10,"M"},{0x11,"N"},{0x12,"O"},
        {0x13,"P"},{0x14,"Q"},{0x15,"R"},{0x16,"S"},{0x17,"T"},
        {0x18,"U"},{0x19,"V"},{0x1A,"W"},{0x1B,"X"},{0x1C,"Y"},
        {0x1D,"Z"},
        {0x1E,"1"},{0x1F,"2"},{0x20,"3"},{0x21,"4"},{0x22,"5"},
        {0x23,"6"},{0x24,"7"},{0x25,"8"},{0x26,"9"},{0x27,"0"},
        {0x28,"ENT"},{0x29,"ESC"},{0x2A,"BSP"},{0x2B,"TAB"},
        {0x2C,"SPC"},{0x2D,"-"},{0x2E,"="},{0x2F,"["},{0x30,"]"},
        {0x31,"\\"},{0x33,";"},{0x34,"'"},{0x35,"`"},{0x36,","},
        {0x37,"."},{0x38,"/"},{0x39,"CAP"},
        {0x3A,"F1"},{0x3B,"F2"},{0x3C,"F3"},{0x3D,"F4"},
        {0x3E,"F5"},{0x3F,"F6"},{0x40,"F7"},{0x41,"F8"},
        {0x42,"F9"},{0x43,"F10"},{0x44,"F11"},{0x45,"F12"},
        {0x4C,"DEL"},{0x4F,"\xe2\x86\x92"},{0x50,"\xe2\x86\x90"},
        {0x51,"\xe2\x86\x93"},{0x52,"\xe2\x86\x91"},
        {0xE0,"LCT"},{0xE1,"LSH"},{0xE2,"LAL"},{0xE3,"LGU"},
        {0xE4,"RCT"},{0xE5,"RSH"},{0xE6,"RAL"},{0xE7,"RGU"},
    };

    char prefix[10] = "";
    if (mods & 0x02 || mods & 0x20) strncat(prefix, "S+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & 0x01 || mods & 0x10) strncat(prefix, "C+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & 0x04 || mods & 0x40) strncat(prefix, "A+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & 0x08 || mods & 0x80) strncat(prefix, "G+", sizeof(prefix) - strlen(prefix) - 1);

    for (size_t i = 0; i < ARRAY_SIZE(tbl); i++) {
        if (tbl[i].kc == kc) {
            snprintf(buf, len, "%s%s", prefix, tbl[i].s);
            return buf;
        }
    }
    snprintf(buf, len, "%s%02X", prefix, (unsigned)(kc & 0xFF));
    return buf;
}

/* ── Layer name strings ───────────────────────────────────── */

static const char *kh_layer_name(uint8_t layer) {
    static const char *names[] = {"BASE", "SYM", "NUM", "FNC"};
    if (layer < ARRAY_SIZE(names)) return names[layer];
    static char fallback[5];
    snprintf(fallback, sizeof(fallback), "L%d", layer);
    return fallback;
}

/* ── Age → opacity ────────────────────────────────────────── */

static lv_opa_t kh_age_opacity(uint32_t age_ms) {
    if (age_ms < 2000) return LV_OPA_100;
    if (age_ms < 4000) return LV_OPA_60;
    if (age_ms < 6000) return LV_OPA_30;
    return LV_OPA_15;
}

/* ── Row renderer ────────────────────────────────────────── */

static void kh_render_row(lv_obj_t *parent, const kh_entry_t *e,
                           uint8_t row_idx, uint32_t age_ms) {
    lv_opa_t opa = kh_age_opacity(age_ms);

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, KH_SCREEN_W, KH_ROW_H);
    lv_obj_set_pos(row, 0, row_idx * KH_ROW_H);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(row,
        e->type == KH_LAYER_CHANGE ? lv_color_hex(0x0d1a0d) : lv_color_black(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_100, 0);

#define ROW_LABEL(x, w, color_hex, font_ptr)              \
    ({                                                     \
        lv_obj_t *_l = lv_label_create(row);             \
        lv_obj_set_pos(_l, (x), 3);                      \
        lv_obj_set_width(_l, (w));                       \
        lv_label_set_long_mode(_l, LV_LABEL_LONG_CLIP); \
        lv_obj_set_style_text_color(_l,                  \
            lv_color_hex(color_hex), 0);                 \
        lv_obj_set_style_text_opa(_l, opa, 0);          \
        lv_obj_set_style_text_font(_l, (font_ptr), 0);  \
        _l;                                              \
    })

    if (e->type == KH_LAYER_CHANGE) {
        lv_obj_t *sym = ROW_LABEL(KH_COL_POS_X, KH_COL_POS_W, 0x3fb950, &lv_font_montserrat_14);
        lv_label_set_text(sym, "\xe2\x97\x86"); /* UTF-8 ◆ */

        char desc[24];
        snprintf(desc, sizeof(desc), "%s %s",
                 e->layer_active ? "act" : "deact",
                 kh_layer_name(e->new_layer));
        lv_obj_t *ev = ROW_LABEL(KH_COL_KC_X, KH_COL_KC_W + KH_COL_BADGE_W,
                                   0x3fb950, &lv_font_montserrat_14);
        lv_label_set_text(ev, desc);

        char ts[10];
        snprintf(ts, sizeof(ts), "%lus", (unsigned long)(age_ms / 1000));
        lv_obj_t *t = ROW_LABEL(KH_COL_TIME_X, KH_COL_TIME_W, 0x30363d, &lv_font_montserrat_12);
        lv_label_set_text(t, ts);
    } else {
        /* Position */
        char pos_str[8];
        if (e->position == 0xFFFFFFFF) {
            snprintf(pos_str, sizeof(pos_str), "??");
        } else {
            snprintf(pos_str, sizeof(pos_str), "#%lu", (unsigned long)e->position);
        }
        lv_obj_t *pos_lbl = ROW_LABEL(KH_COL_POS_X, KH_COL_POS_W, 0x7c6af7, &lv_font_montserrat_14);
        lv_label_set_text(pos_lbl, pos_str);

        /* Arrow */
        lv_obj_t *arr = ROW_LABEL(KH_COL_ARR_X, KH_COL_ARR_W, 0x30363d, &lv_font_montserrat_14);
        lv_label_set_text(arr, "\xe2\x86\x92"); /* UTF-8 → */

        /* Keycode */
        char kc_buf[16];
        const char *kc_str = (e->keycode == 0)
            ? "..."
            : kh_kc_str(e->keycode, e->mods, kc_buf, sizeof(kc_buf));
        lv_obj_t *kc_lbl = ROW_LABEL(KH_COL_KC_X, KH_COL_KC_W, 0xe6edf3, &lv_font_montserrat_14);
        lv_label_set_text(kc_lbl, kc_str);

        /* Layer badge */
        kh_layer_color_t lc = kh_layer_color(e->layer);
        lv_obj_t *badge_bg = lv_obj_create(row);
        lv_obj_set_pos(badge_bg, KH_COL_BADGE_X, 3);
        lv_obj_set_size(badge_bg, KH_COL_BADGE_W, KH_ROW_H - 6);
        lv_obj_set_style_bg_color(badge_bg, lv_color_hex(lc.bg), 0);
        lv_obj_set_style_bg_opa(badge_bg, opa, 0);
        lv_obj_set_style_border_width(badge_bg, 0, 0);
        lv_obj_set_style_radius(badge_bg, 2, 0);
        lv_obj_set_style_pad_all(badge_bg, 0, 0);
        lv_obj_clear_flag(badge_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *badge_lbl = lv_label_create(badge_bg);
        lv_label_set_text(badge_lbl, kh_layer_name(e->layer));
        lv_obj_set_style_text_color(badge_lbl, lv_color_hex(lc.text), 0);
        lv_obj_set_style_text_opa(badge_lbl, opa, 0);
        lv_obj_set_style_text_font(badge_lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(badge_lbl);

        /* Timestamp */
        char ts[10];
        snprintf(ts, sizeof(ts), "%lus", (unsigned long)(age_ms / 1000));
        lv_obj_t *t = ROW_LABEL(KH_COL_TIME_X, KH_COL_TIME_W, 0x30363d, &lv_font_montserrat_12);
        lv_label_set_text(t, ts);
    }

#undef ROW_LABEL
}

/* ── Screen create / rebuild ────────────────────────────── */

lv_obj_t *kh_screen_create(void) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, KH_SCREEN_W, KH_SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_100, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t *header = lv_obj_create(s_screen);
    lv_obj_set_size(header, KH_SCREEN_W, KH_HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_100, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "KEY HISTORY");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(title, 8, 8);

    lv_obj_t *hint = lv_label_create(header);
    lv_label_set_text(hint, "j/k=scroll  H=close");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x484f58), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -6, 0);

    /* List container */
    s_list_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_list_cont, KH_SCREEN_W, KH_LIST_H);
    lv_obj_set_pos(s_list_cont, 0, KH_HEADER_H);
    lv_obj_set_style_bg_color(s_list_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_list_cont, LV_OPA_100, 0);
    lv_obj_set_style_border_width(s_list_cont, 0, 0);
    lv_obj_set_style_pad_all(s_list_cont, 0, 0);
    lv_obj_clear_flag(s_list_cont, LV_OBJ_FLAG_SCROLLABLE);

    return s_screen;
}

void kh_screen_rebuild(void) {
    if (!s_list_cont) return;
    lv_obj_clean(s_list_cont);

    uint8_t count  = kh_count();
    uint8_t offset = kh_get_scroll_offset();

    /* Get the newest entry's timestamp for age calculation */
    kh_entry_t newest;
    uint32_t newest_ts = 0;
    if (kh_get(0, &newest)) {
        newest_ts = newest.timestamp_ms;
    }

    uint8_t row  = 0;
    uint8_t skip = offset; /* entries to skip from newest end */
    uint8_t src  = 0;      /* ring buffer index (0=newest) */

    while (row < KH_VISIBLE_ROWS && src < count) {
        kh_entry_t e;
        if (!kh_get(src, &e)) break;
        src++;

        /* Skip key releases — not shown */
        if (e.type == KH_KEY_RELEASE) continue;

        if (skip > 0) {
            skip--;
            continue;
        }

        uint32_t age_ms = (newest_ts >= e.timestamp_ms)
                          ? (newest_ts - e.timestamp_ms) : 0;
        kh_render_row(s_list_cont, &e, row, age_ms);
        row++;
    }
}
