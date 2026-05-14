# Recovery

## Factory Backup

A full factory firmware backup should always be preserved.

Example:
backup_factory_full.bin

---

# Safety Rules

Never:
- erase flash casually
- overwrite recovery images
- remove OTA rollback capability

without explicit confirmation.

---

# Recovery Strategy

Preferred future layout:

- factory partition
- OTA slot A
- OTA slot B
- NVS
- storage/assets

---

# Recovery Goal

The panel should always be recoverable
even after failed OTA updates.
