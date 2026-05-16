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
