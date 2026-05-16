# Storage System

## Persistent Storage

Stored examples:
- Wi‑Fi credentials
- HA credentials
- brightness
- volume
- preferences

## Runtime Cache

Examples:
- recent entity states
- notification cache
- assistant context

## Questions

The architecture must define:
- what survives reboot
- what survives OTA
- what survives factory reset

## OTA Layout Impact

The proposed OTA layout preserves a dedicated `storage` partition, but it is
smaller than the current factory-only layout.

Before activating the OTA partition table, decide which records live in NVS and
which records live in the filesystem-backed storage partition. Any migration
that rewrites the partition table must treat existing persistent data as
disposable unless it is backed up first.
