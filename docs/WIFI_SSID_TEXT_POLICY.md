# Wi-Fi SSID Text Policy

Wi-Fi SSIDs may contain accented Latin characters and other Unicode characters.

Examples:
- Café
- paLANtír
- München
- Pokémon

## Requirement

SSID rendering must support:
- ASCII
- Latin-1 Supplement
- Latin Extended-A
- common punctuation
- symbols commonly used in names

## Behaviour

SSID text must:
- preserve UTF-8 encoding
- render accented characters correctly when glyphs exist
- use fallback fonts when primary font lacks glyphs
- truncate safely if too long
- never corrupt the Wi-Fi setup UI

## Test Strings

- Cafe
- Café
- paLANtír
- München
- Pokémon
- 家庭WiFi
- Elvin_5GHz
- Network-测试
