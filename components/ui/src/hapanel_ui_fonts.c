#include "hapanel_ui_fonts.h"

#include "esp_log.h"

#include <inttypes.h>
#include <stddef.h>

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
    return true;
}

static bool font_chain_has_glyph(const lv_font_t *font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph = {0};
    return lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) && glyph.resolved_font != NULL &&
           !glyph.is_placeholder;
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

    if (!missing_log_capacity_warned) {
        ESP_LOGW(TAG, "missing glyph log capacity reached; suppressing additional codepoints");
        missing_log_capacity_warned = true;
    }

    return false;
}

static bool is_dynamic_font_known_coverage(uint32_t codepoint)
{
    return (codepoint >= 0x20 && codepoint <= 0x7e) ||
           (codepoint >= 0xa0 && codepoint <= 0x17f);
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

static const lv_font_t *hapanel_ui_font_safe_dynamic_16(void)
{
    return &lv_font_montserrat_16;
}

static void append_ascii(char *output, size_t output_size, size_t *offset, const char *text)
{
    if (output_size == 0 || output == NULL || offset == NULL || text == NULL) {
        return;
    }

    while (*text != '\0' && *offset + 1 < output_size) {
        output[(*offset)++] = *text++;
    }
}

static void append_text_bytes(char *output,
                              size_t output_size,
                              size_t *offset,
                              const char *text,
                              size_t byte_count)
{
    if (output_size == 0 || output == NULL || offset == NULL || text == NULL) {
        return;
    }

    for (size_t i = 0; i < byte_count && *offset + 1 < output_size; ++i) {
        output[(*offset)++] = text[i];
    }
}

static const char *ascii_fallback_for_codepoint(uint32_t codepoint)
{
    switch (codepoint) {
    case 0x00b0:
        return " deg";
    case 0x00d7:
        return "x";
    case 0x00df:
        return "ss";
    case 0x00c0:
    case 0x00c1:
    case 0x00c2:
    case 0x00c3:
    case 0x00c4:
    case 0x00c5:
    case 0x0100:
    case 0x0102:
    case 0x0104:
        return "A";
    case 0x00e0:
    case 0x00e1:
    case 0x00e2:
    case 0x00e3:
    case 0x00e4:
    case 0x00e5:
    case 0x0101:
    case 0x0103:
    case 0x0105:
        return "a";
    case 0x00c7:
    case 0x0106:
    case 0x0108:
    case 0x010a:
    case 0x010c:
        return "C";
    case 0x00e7:
    case 0x0107:
    case 0x0109:
    case 0x010b:
    case 0x010d:
        return "c";
    case 0x00c8:
    case 0x00c9:
    case 0x00ca:
    case 0x00cb:
    case 0x0112:
    case 0x0114:
    case 0x0116:
    case 0x0118:
    case 0x011a:
        return "E";
    case 0x00e8:
    case 0x00e9:
    case 0x00ea:
    case 0x00eb:
    case 0x0113:
    case 0x0115:
    case 0x0117:
    case 0x0119:
    case 0x011b:
        return "e";
    case 0x00cc:
    case 0x00cd:
    case 0x00ce:
    case 0x00cf:
        return "I";
    case 0x00ec:
    case 0x00ed:
    case 0x00ee:
    case 0x00ef:
        return "i";
    case 0x00d1:
        return "N";
    case 0x00f1:
        return "n";
    case 0x00d2:
    case 0x00d3:
    case 0x00d4:
    case 0x00d5:
    case 0x00d6:
    case 0x00d8:
        return "O";
    case 0x00f2:
    case 0x00f3:
    case 0x00f4:
    case 0x00f5:
    case 0x00f6:
    case 0x00f8:
        return "o";
    case 0x00d9:
    case 0x00da:
    case 0x00db:
    case 0x00dc:
        return "U";
    case 0x00f9:
    case 0x00fa:
    case 0x00fb:
    case 0x00fc:
        return "u";
    case 0x00dd:
    case 0x0178:
        return "Y";
    case 0x00fd:
    case 0x00ff:
        return "y";
    case 0x2013:
    case 0x2014:
    case 0x2212:
        return "-";
    case 0x2018:
    case 0x2019:
        return "'";
    case 0x201c:
    case 0x201d:
        return "\"";
    case 0x2026:
        return "...";
    case 0x2713:
        return "OK";
    default:
        return "?";
    }
}

const lv_font_t *hapanel_ui_font_prepare_dynamic_text(const char *input,
                                                      char *output,
                                                      size_t output_size)
{
    if (output == NULL || output_size == 0) {
        return hapanel_ui_font_safe_dynamic_16();
    }

    output[0] = '\0';
    if (input == NULL) {
        return hapanel_ui_font_safe_dynamic_16();
    }

    size_t input_offset = 0;
    size_t output_offset = 0;
    uint32_t codepoint = 0;
    bool needs_extended_font = false;
    const lv_font_t *extended_font = hapanel_ui_font_dynamic_16();
    while (output_offset + 1 < output_size) {
        const size_t codepoint_offset = input_offset;
        if (!decode_utf8_next(input, &input_offset, &codepoint)) {
            break;
        }
        const size_t codepoint_size = input_offset - codepoint_offset;
        if (codepoint >= 0x20 && codepoint <= 0x7e) {
            output[output_offset++] = (char)codepoint;
        } else if (codepoint == '\n' || codepoint == '\r' || codepoint == '\t') {
            output[output_offset++] = ' ';
        } else if (font_chain_has_glyph(extended_font, codepoint)) {
            append_text_bytes(output,
                              output_size,
                              &output_offset,
                              &input[codepoint_offset],
                              codepoint_size);
            needs_extended_font = true;
        } else {
            append_ascii(output,
                         output_size,
                         &output_offset,
                         ascii_fallback_for_codepoint(codepoint));
        }
    }
    output[output_offset] = '\0';
    return needs_extended_font ? extended_font : hapanel_ui_font_safe_dynamic_16();
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

        if (is_dynamic_font_known_coverage(codepoint)) {
            continue;
        }

        if (!font_chain_has_glyph(font, codepoint) && should_log_missing_codepoint(codepoint)) {
            ESP_LOGW(TAG, "missing glyph: surface=%s codepoint=U+%04" PRIX32, surface, codepoint);
        }
    }
}
