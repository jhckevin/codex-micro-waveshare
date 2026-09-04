# Codex BLE HIDD patch

This component is vendored from ESP-IDF 5.4.2.

The local patch adds `esp_hidd_dev_restore_bonded_cccs()`. Bluedroid's HIDD
component stores Client Characteristic Configuration values only in RAM, even
though a bonded client expects its notification subscriptions to survive a
device reboot. The Codex transport calls the restoration API only after BLE
authentication and slot validation. Restoration intentionally precedes host
HID output because Windows may wait for the bonded notification subscriptions
before reopening the vendor HID data path.

Keeping this as a project component makes the firmware build reproducible and
avoids modifying the remote ESP-IDF installation.
