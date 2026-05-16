# Character Support Policy

HAPanel must support UTF-8 text throughout the firmware.

The system should minimize missing-glyph rectangles, especially in:
- Wi-Fi SSIDs
- Home Assistant entity names
- notifications
- calendar entries
- news headlines
- AI assistant responses and subtitles

## Core Principle

UTF-8 support and glyph support are different concerns.

The firmware must:
1. store and process text as UTF-8
2. render text through fonts that contain the required glyphs
3. use fallback fonts for dynamic external text
4. degrade gracefully when a glyph is unavailable

## Required Behaviour

Missing glyphs must never:
- crash the firmware
- corrupt layout
- break navigation
- freeze the UI

When possible, missing glyphs should:
- render as a replacement glyph
- be logged with Unicode codepoint
- be counted for later font-subset expansion

## Static vs Dynamic Text

Static UI text:
- may use small curated fonts
- should mostly use English
- should avoid unnecessary rare characters

Dynamic text:
- must use fallback-capable font chains
- includes SSIDs, AI text, notifications, entity names, calendar titles, news, and user-generated text

## Non-Goal

Do not attempt full Unicode coverage initially.

Full Unicode font coverage is usually too large for ESP32-class devices
and should be replaced with curated subsets, fallback chains, and missing-glyph logging.
