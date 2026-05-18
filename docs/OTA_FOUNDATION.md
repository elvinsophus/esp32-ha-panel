# OTA Foundation

HAPanel's OTA service starts as an inspection and status layer.

Current behavior:
- logs the running and configured boot partitions
- checks whether `ota_0` and `ota_1` app slots exist
- reports OTA status to the system status UI
- reports OTA image state when rollback metadata exists
- marks a pending-verify OTA image valid after display, touch, and root UI
  initialization succeed
- runs an OTA preflight check that selects the next update slot and blocks
  updates while the running image still needs validation
- exposes the read-only OTA preflight result and partition inventory in retained
  MQTT device state and Home Assistant diagnostic entities
- exposes structured OTA progress in retained MQTT device state
- exposes a transport-agnostic OTA install session API for begin, write,
  finish, and abort
- can install a firmware image from a plain HTTP URL with a known
  `Content-Length`, using the same install session API
- exposes an opt-in local OTA self-test staging path that copies the running
  image into the next OTA slot through the install session API
- rejects MQTT-triggered OTA self-test staging unless
  `CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE` is explicitly enabled

Current limitation:
- HTTPS OTA is not enabled yet; the first transport is plain HTTP for local
  controlled bring-up while certificate handling is designed

Safety boundary:
- no flash write happens during normal boot
- no boot partition switch happens during normal boot
- no partition layout change is made by the service itself

Before enabling real OTA updates, the partition and recovery strategy must be
designed explicitly.

## Proposed 16MB Partition Strategy

The active `partitions.csv` now uses the OTA/recovery layout that was first
introduced in `partitions_ota_proposed.csv`.

Recommended layout:
- `nvs`: 16KB for Wi-Fi, preferences, and small configuration records
- `otadata`: 8KB for ESP-IDF OTA slot selection metadata
- `phy_init`: 4KB for RF calibration data
- `factory`: 3MB recovery firmware
- `ota_0`: 4MB primary OTA application slot
- `ota_1`: 4MB alternate OTA application slot
- `storage`: remaining 0x4F0000 bytes for persistent app data

Rationale:
- keeps a dedicated factory recovery image
- gives both OTA slots room to grow beyond the current firmware size
- preserves several megabytes of persistent storage
- keeps the active layout aligned with the OTA service readiness check

Activation requirements:
- decide whether the factory image is a minimal recovery image or a full UI image
- back up any user data that must survive partition migration
- confirm whether NVS may be reinitialized during the first layout migration
- verify boot, display, touch, storage, and OTA slot detection on hardware

## First Activation Notes

The first activation on the test panel required erasing only the new NVS region
at `0x9000..0xcfff` after flashing the new table. The previous factory-only NVS
partition was larger, and stale metadata prevented `nvs_flash_init()` from
opening the resized NVS partition cleanly.

## Rollback Validation

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is enabled in `sdkconfig.defaults`.
When a future OTA update boots from `ota_0` or `ota_1` in
`ESP_OTA_IMG_PENDING_VERIFY`, HAPanel marks the image valid only after the
panel has initialized PSRAM, NVS, display, touch, OTA status, and the root UI.

Network and Home Assistant availability are intentionally not part of the boot
validity gate. A panel should not roll back merely because Wi-Fi credentials are
missing or Home Assistant is offline.

## Update Preflight

`hapanel_ota_preflight()` is the safe gate that future MQTT, HTTP, or local
update triggers must pass before starting an OTA write. It reports:
- whether an update may start
- why it is blocked, if blocked
- the currently running app partition
- the target OTA slot, address, and size

The preflight does not erase, write, download, or switch boot partitions.

## Partition Inventory

`hapanel_ota_get_inventory()` exposes the read-only OTA partition snapshot used
by MQTT diagnostics. It reports the running, boot, factory, `ota_0`, `ota_1`,
and next target partitions, whether the configured boot partition matches the
running partition, whether rollback is enabled, and the running OTA image state
when ESP-IDF exposes it. It also derives a normalized phase:
- `ready`: no OTA action is pending
- `reboot_needed`: a staged image is selected as the next boot target
- `pending_verify`: the running image still needs rollback validation
- `invalid`: ESP-IDF reports the running image invalid or aborted
- `partition_error`: running or boot partition lookup failed

## Install Session

`hapanel_ota_begin()`, `hapanel_ota_write()`, `hapanel_ota_finish()`, and
`hapanel_ota_abort()` provide the transport-independent install engine.

The session API:
- requires a known non-zero image size
- rejects images larger than the selected OTA slot
- runs preflight before erasing the target slot
- writes only to the selected inactive OTA partition
- validates the image with `esp_ota_end()`
- switches the next boot partition only after validation succeeds
- reports `Reboot needed` instead of restarting automatically

No current boot path calls this session API. MQTT, HTTP, or local update
triggers must call it explicitly.

## HTTP Install Transport

`hapanel_ota_install_from_http_url()` downloads an application image from a
plain `http://` URL with a known `Content-Length`. It:
- rejects empty, non-HTTP, or unknown-size responses
- rejects non-2xx HTTP status codes
- writes through `hapanel_ota_begin()`, `hapanel_ota_write()`, and
  `hapanel_ota_finish()`
- reports coarse write progress in the OTA status text
- updates `ota.progress` in MQTT state with phase, target slot, byte counts,
  and percent complete
- stages the image for the next boot only after ESP-IDF validates it
- never restarts the panel automatically

The paired MQTT `ota_reboot` command is the guarded restart path. It is rejected
unless `hapanel_ota_get_inventory()` reports `reboot_required=true`.

This transport is intentionally narrow for the first real OTA milestone.
HTTPS should be added once the project has an explicit CA/certificate storage
strategy.

## Local Self-Test Staging

`hapanel_ota_self_test_stage_running()` copies the currently running app image
into the next OTA slot using the same begin/write/finish API that future
transports will use. It is intended for controlled bring-up tests of slot
selection, `otadata`, reboot, pending verification, and rollback validation.

The self-test API is not called during normal boot. When it is eventually
triggered, it stages the copied image and reports `Reboot needed`; it does not
restart the panel automatically.

For controlled hardware bring-up, `CONFIG_HAPANEL_OTA_SELF_TEST_STAGE_FACTORY_ON_BOOT`
can be enabled in a local `sdkconfig` build. This switch is default-off and only
stages when the running partition is `factory`, preventing an OTA image from
restaging itself on every boot.

For explicit admin-triggered bring-up,
`CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE` enables the
`ota_self_test_stage` MQTT command. This command can stage the currently running
image from any app partition into the inactive OTA slot and set that slot as the
next boot target. It is default-off, writes flash only when enabled and invoked,
and never restarts the panel automatically.
