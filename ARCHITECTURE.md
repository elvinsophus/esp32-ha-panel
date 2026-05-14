# Architecture

## High-Level Structure

Firmware layers:

Application
↓
UI layer (LVGL)
↓
Services
↓
Drivers / hardware abstraction
↓
ESP-IDF / FreeRTOS
↓
ESP32-S3 hardware

---

# Planned Components

components/
    ui/
    mqtt/
    ha/
    storage/
    network/
    touch/
    display/
    ota/
    services/

---

# Core Principles

- modular
- event-driven
- non-blocking
- recoverable
- maintainable

---

# UI Architecture

Planned UI model:

- page manager
- reusable widgets
- global theme system
- navigation stack
- transition manager
- notification overlay system

---

# State Management

Future architecture should centralize:
- network state
- HA connection state
- MQTT state
- UI state
- sensor cache

Avoid scattered globals.
