# Codex Tasks: Unicode and Font Support

## Immediate Tasks

1. Verify LVGL is configured for UTF-8.
2. Add or document a font registry abstraction.
3. Ensure Wi-Fi SSID rendering uses a Latin-extended-capable font.
4. Add dynamic text surfaces with fallback font support.
5. Add a test list for accented SSIDs and mixed-language strings.
6. Add missing-glyph handling policy.

## Do Not Do Yet

- Do not embed full Unicode fonts.
- Do not make CJK fonts the default UI font.
- Do not hardcode font paths throughout UI code.
- Do not block UI rendering while loading large font assets.

## Acceptance Criteria

- Accented SSIDs render correctly.
- Dynamic text does not crash or corrupt layout.
- Missing glyphs degrade gracefully.
- Font choices remain configurable through profiles.
- Static UI and dynamic text have separate font policies.
