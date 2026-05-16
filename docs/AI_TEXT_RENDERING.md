# AI Text Rendering

AI assistant responses are open-ended and may contain arbitrary characters.

AI text must use the dynamic text rendering path, not the static UI font path.

## Initial Language Scope

- English
- Simplified Chinese

The assistant backend should be encouraged, where practical, to respond in supported languages.

## Rendering Requirements

AI text surfaces must:
- support UTF-8
- use fallback fonts
- wrap text safely
- avoid layout corruption
- truncate or scroll gracefully when too long
- tolerate missing glyphs

## Style Requirements

AI subtitles should be:
- readable
- calm
- visually integrated with the assistant expression
- not overly dense
- not visually dominant unless the assistant view is expanded
