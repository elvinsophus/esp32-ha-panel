# HAPanel Agent Instructions

## Project Identity

HAPanel is a custom standalone embedded touch-panel firmware
for the Waveshare ESP32-S3-Touch-LCD-4B.

This is NOT:
- an ESPHome dashboard
- an Arduino toy project
- a Linux system
- a web frontend running in a browser

This IS:
- a production-style embedded firmware
- built with ESP-IDF
- using LVGL
- intended for long-term Home Assistant integration
- designed as a modular panel OS

---

# Hardware

Board:
- Waveshare ESP32-S3-Touch-LCD-4B

MCU:
- ESP32-S3

Memory:
- 16MB Flash
- 8MB PSRAM

Display:
- 480x480 RGB LCD

Touch:
- GT911 capacitive touch

Framework:
- ESP-IDF v6

GUI:
- LVGL v9

Connectivity:
- Wi-Fi
- MQTT
- OTA firmware updates

---

# Architecture Philosophy

The project must prioritize:

- modularity
- maintainability
- responsiveness
- stability
- recoverability
- elegant UX

Avoid:
- giant monolithic files
- blocking operations
- deeply coupled modules
- spaghetti state management

Preferred architecture:

components/
    ui/
    mqtt/
    ha/
    services/
    drivers/
    storage/
    network/

Each major subsystem should be isolated.

---

# UI Philosophy

Target visual style:

- minimalist
- elegant
- premium
- calm
- dark-theme-first
- subtle animations
- no excessive colors
- no gamer aesthetics
- no clutter

The panel should resemble:
- premium smart-home panels
- modern automotive HMIs
- refined industrial interfaces

Avoid:
- rainbow RGB aesthetics
- childish widgets
- overly dense dashboards

---

# Current Milestone

Current development phase:

1. Boot successfully
2. Initialize LCD
3. Initialize touch
4. Connect Wi-Fi
5. Connect MQTT
6. Display basic status UI
7. Implement OTA
8. Build foundational architecture

DO NOT prematurely implement:
- advanced dashboards
- complex HA automation logic
- voice assistant systems
- camera streaming
- AI features

Foundation first.

---

# Home Assistant Integration

Preferred integration:
1. MQTT first
2. Native HA WebSocket later if needed

The firmware should remain functional even if:
- HA is offline
- MQTT is unavailable
- Wi-Fi disconnects temporarily

Graceful degradation is important.

---

# Embedded Constraints

Important constraints:

- RAM is limited
- Avoid heap fragmentation
- Use PSRAM carefully
- Avoid unnecessary full-screen redraws
- Prefer async/event-driven design
- UI must remain responsive

Avoid:
- unnecessary dynamic allocation
- large temporary buffers
- blocking delays in UI tasks

---

# OTA and Recovery

OTA support is required.

A recovery/factory image strategy must exist.

Agents must NOT:
- erase flash automatically
- overwrite recovery images
- change partition layouts recklessly

without explicit approval.

---

# Allowed Agent Actions

Agents MAY:
- edit source files
- refactor modules
- reorganize architecture
- modify CMake files
- add components
- run builds/tests

Agents MUST NOT:
- flash firmware automatically
- erase flash
- overwrite factory backups
- modify security-sensitive credentials

without explicit user confirmation.

---

# Long-Term Vision

Future planned capabilities:

- Home Assistant control
- plant monitoring integration
- Frigate/camera integration
- notification center
- voice assistant integration
- local automation panels
- room-specific UI pages
- elegant transitions/animations

This should evolve into a polished embedded smart-home control system,
not merely a touchscreen MQTT client.
