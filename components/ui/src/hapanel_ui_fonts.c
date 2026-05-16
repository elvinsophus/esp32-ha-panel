#include "hapanel_ui_fonts.h"

#include "esp_log.h"

#include <inttypes.h>

static const char *TAG = "hapanel_ui_fonts";

LV_FONT_DECLARE(hapanel_font_dynamic_16);

static bool dynamic_fonts_ready;
static lv_font_t dynamic_font_16;
static uint32_t logged_missing_codepoints[32];
static bool missing_log_capacity_warned;

static void ensure_dynamic_font_chain(void)
{
    if (dynamic_fonts_ready) {
        return;
    }

    dynamic_font_16 = hapanel_font_dynamic_16;
    dynamic_font_16.fallback = &lv_font_montserrat_16;
    dynamic_fonts_ready = true;
}

static bool decode_utf8_next(const char *text, size_t *offset, uint32_t *codepoint)
{
    const uint8_t *bytes = (const uint8_t *)text;
    const uint8_t first = bytes[*offset];

    if (first == '\0') {
        *codepoint = 0;
        return false;
    }

    if (first < 0x80) {
        *codepoint = first;
        *offset += 1;
        return true;
    }

    uint32_t value = 0;
    size_t length = 0;
    uint32_t min_value = 0;

    if ((first & 0xe0) == 0xc0) {
        value = first & 0x1f;
        length = 2;
        min_value = 0x80;
    } else if ((first & 0xf0) == 0xe0) {
        value = first & 0x0f;
        length = 3;
        min_value = 0x800;
    } else if ((first & 0xf8) == 0xf0) {
        value = first & 0x07;
        length = 4;
        min_value = 0x10000;
    } else {
        *codepoint = 0xfffd;
        *offset += 1;
        return true;
    }

    for (size_t i = 1; i < length; ++i) {
        const uint8_t next = bytes[*offset + i];
        if ((next & 0xc0) != 0x80) {
            *codepoint = 0xfffd;
            *offset += 1;
            return true;
        }
        value = (value << 6) | (next & 0x3f);
    }

    if (value < min_value || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        *codepoint = 0xfffd;
    } else {
        *codepoint = value;
    }

    *offset += length;
    if (!missing_log_capacity_warned) {
        ESP_LOGW(TAG, "missing glyph log capacity reached; suppressing additional codepoints");
        missing_log_capacity_warned = true;
    }

    return false;
}

static bool font_chain_has_glyph(const lv_font_t *font, uint32_t codepoint)
{
    for (const lv_font_t *candidate = font; candidate != NULL; candidate = candidate->fallback) {
        lv_font_glyph_dsc_t glyph = {0};
        if (candidate->get_glyph_dsc(candidate, &glyph, codepoint, 0) && !glyph.is_placeholder) {
            return true;
        }
    }

    return false;
}

static bool should_log_missing_codepoint(uint32_t codepoint)
{
    for (size_t i = 0; i < sizeof(logged_missing_codepoints) / sizeof(logged_missing_codepoints[0]);
         ++i) {
        if (logged_missing_codepoints[i] == codepoint) {
            return false;
        }
    }

    for (size_t i = 0; i < sizeof(logged_missing_codepoints) / sizeof(logged_missing_codepoints[0]);
         ++i) {
        if (logged_missing_codepoints[i] == 0) {
            logged_missing_codepoints[i] = codepoint;
            return true;
        }
    }

    return true;
}

const lv_font_t *hapanel_ui_font_static_12(void)
{
    return &lv_font_montserrat_12;
}

const lv_font_t *hapanel_ui_font_static_16(void)
{
    return &lv_font_montserrat_16;
}

const lv_font_t *hapanel_ui_font_static_18(void)
{
    return &lv_font_montserrat_18;
}

const lv_font_t *hapanel_ui_font_static_26(void)
{
    return &lv_font_montserrat_26;
}

const lv_font_t *hapanel_ui_font_dynamic_16(void)
{
    ensure_dynamic_font_chain();
    return &dynamic_font_16;
}

void hapanel_ui_font_log_missing_glyphs(const char *surface, const char *text,
                                        const lv_font_t *font)
{
    if (surface == NULL || text == NULL || font == NULL) {
        return;
    }

    size_t offset = 0;
    uint32_t codepoint = 0;

    while (decode_utf8_next(text, &offset, &codepoint)) {
        if (codepoint == 0 || codepoint == '\n' || codepoint == '\r' || codepoint == '\t') {
            continue;
        }

        if (!font_chain_has_glyph(font, codepoint) && should_log_missing_codepoint(codepoint)) {
            ESP_LOGW(TAG, "missing glyph: surface=%s codepoint=U+%04" PRIX32, surface, codepoint);
        }
    }
}
