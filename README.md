# HAPanel

HAPanel is a standalone embedded Home Assistant control panel firmware
for the Waveshare ESP32-S3-Touch-LCD-4B.

Built with:
- ESP-IDF
- LVGL
- MQTT
- OTA support

## Goals

- elegant embedded UI
- smooth touch interaction
- robust architecture
- standalone operation
- modular design
- Home Assistant integration

## Hardware

- ESP32-S3
- 16MB Flash
- 8MB PSRAM
- 480x480 touch LCD

## Current Status

Early architecture and hardware bring-up phase.

Current firmware boots the board support package display stack and shows a
minimal HAPanel system status screen. Wi-Fi, MQTT, OTA, and Home Assistant
logic are planned foundation services, not active dashboard features yet.
