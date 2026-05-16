# Font Generation Workflow

Font assets should be reproducible and generated from declared source fonts and glyph ranges.

## Recommended Workflow

1. Choose source fonts.
2. Define coverage ranges and custom text lists.
3. Generate LVGL-compatible fonts.
4. Register fonts in the font registry.
5. Configure fallback chains.
6. Run glyph coverage tests.
7. Add missing glyphs based on logs.

## Source Fonts

Recommended families:
- Inter or similar for Latin UI
- Noto Sans or Source Sans for broader Latin
- Noto Sans SC / HarmonyOS Sans SC / MiSans for Simplified Chinese fallback

Do not commit commercial fonts unless licensing permits it.

## Coverage Files

Maintain explicit coverage files:

assets/fonts/coverage/
- ui_latin_extended.txt
- dynamic_symbols.txt
- cjk_common_sc.txt
- observed_missing_glyphs.txt

## Build Integration

Font generation should be automated eventually, but may begin as a manual process.

Codex should avoid hardcoding font names or paths throughout the codebase.
Use a font registry abstraction instead.
