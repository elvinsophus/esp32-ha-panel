# OTA Foundation

HAPanel's OTA service starts as an inspection and status layer.

Current behavior:
- logs the running and configured boot partitions
- checks whether `ota_0` and `ota_1` app slots exist
- reports OTA status to the system status UI

Current limitation:
- the active partition table is factory-only, so OTA reports `Factory only`

Safety boundary:
- no image download is implemented yet
- no flash write is implemented yet
- no boot partition switch is implemented yet
- no partition layout change is made by the service

Before enabling real OTA updates, the partition and recovery strategy must be
designed explicitly.

## Proposed 16MB Partition Strategy

The repository includes `partitions_ota_proposed.csv` as a non-active proposal.
It is not used by the build yet.

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
- avoids changing the active flash layout before the recovery plan is approved

Activation requirements:
- decide whether the factory image is a minimal recovery image or a full UI image
- back up any user data that must survive partition migration
- confirm whether NVS may be reinitialized during the first layout migration
- flash the new partition table only with explicit approval
- verify boot, display, touch, storage, and OTA slot detection on hardware
