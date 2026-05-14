# MQTT Integration

## Philosophy

MQTT is the primary Home Assistant integration method.

The panel should:
- remain operational without HA
- gracefully handle reconnects
- cache important states

---

# Planned Features

- entity subscriptions
- command publishing
- retained state handling
- reconnect logic
- offline detection

---

# Design Goals

- resilient
- lightweight
- async
- low-latency
