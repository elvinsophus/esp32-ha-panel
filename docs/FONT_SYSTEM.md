# Font System

The font system provides elegant typography while keeping flash, RAM, and rendering cost under control.

## Font Classes

### UI Primary Font

Used for:
- system labels
- root page
- buttons
- navigation
- settings
- static UI

Coverage:
- ASCII
- Latin-1 Supplement
- Latin Extended-A
- common punctuation
- common symbols
- units

### Dynamic Text Font

Used for:
- Wi-Fi SSIDs
- HA entity names
- notifications
- calendar titles
- AI subtitles
- news headlines

Coverage should be broader than UI Primary and must support fallback chains.

### CJK Fallback Font

Used only when needed for:
- assistant responses
- Chinese notifications
- Chinese names
- multilingual text

This should be subsetted aggressively.

### Symbol / Icon Font

Used for:
- status icons
- UI symbols
- small semantic glyphs

## Fallback Chain

A typical fallback chain:

1. ui_primary_latin_extended
2. dynamic_symbols_subset
3. cjk_simplified_subset
4. replacement glyph

## LVGL Requirements

The project must ensure:
- LV_TXT_ENC is configured as UTF-8
- font fallback is available for dynamic text
- font resources are loaded consistently
- all text files and source files are saved as UTF-8

## Missing-Glyph Feedback Loop

1. render dynamic text
2. detect missing glyphs where possible
3. log missing codepoints
4. aggregate missing codepoints
5. regenerate font subsets
6. ship updated font assets
