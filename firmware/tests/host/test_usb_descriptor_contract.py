from pathlib import Path


root = Path(__file__).resolve().parents[2]
usb = (root / "components/codex_transport/usb_transport.cpp").read_text(
    encoding="utf-8"
)
ble = (root / "components/codex_transport/ble_transport.cpp").read_text(
    encoding="utf-8"
)
ble_hidd = (root / "components/esp_hid/src/ble_hidd.c").read_text(
    encoding="utf-8"
)
sdkconfig_defaults = (root / "sdkconfig.defaults").read_text(encoding="utf-8")
release_defaults = (root / "sdkconfig.release.defaults").read_text(
    encoding="utf-8"
)
app_main = (root / "main/app_main.cpp").read_text(encoding="utf-8")

# macOS and Windows both enumerate this as a standards-based IAD composite:
# vendor HID for Codex RPC plus CDC ACM for local configuration.
assert ".bcdUSB = 0x0200" in usb
assert ".bDeviceClass = TUSB_CLASS_MISC" in usb
assert ".bDeviceSubClass = MISC_SUBCLASS_COMMON" in usb
assert ".bDeviceProtocol = MISC_PROTOCOL_IAD" in usb
assert "TUD_HID_INOUT_DESCRIPTOR" in usb
assert "TUD_CDC_DESCRIPTOR" in usb
assert "HID_ITF_PROTOCOL_NONE" in usb
assert "0x06, 0x00, 0xFF" in usb  # vendor-defined HID usage page 0xFF00
assert ".bcdDevice = 0x0100" in usb  # Codex fallback discovery classifies % 4 == 0 as USB

# Handshake/configuration replies are connection-critical.  They are queued
# asynchronously, but must receive the longer bounded endpoint wait instead of
# the droppable status/animation path.
assert "dispatch.reply_length - 1U, pdMS_TO_TICKS(50), true" in usb
assert usb.count("pdMS_TO_TICKS(50), true") >= 4

# BLE uses standard HOGP and Secure Connections bonding without a
# Windows-only pairing mechanism.
assert "ESP_HID_TRANSPORT_BLE" in ble
assert "ESP_LE_AUTH_REQ_SC_BOND" in ble
assert "ESP_IO_CAP_NONE" in ble
assert "esp_hidd_dev_battery_status_set" in ble
assert "0x2BED" in ble_hidd
assert "esp_ble_gatts_set_attr_value(dev->bat_level_handle" in ble_hidd
assert "esp_ble_gatts_set_attr_value(dev->bat_status_handle" in ble_hidd

# The Battery Level Status characteristic was added after development hosts
# had already bonded. Database Hash / robust caching is part of the transport
# contract: without it Windows can retain the former HID Output handle and
# route the first 63-byte Codex RPC packet into a 2-byte BAS attribute.
assert "CONFIG_BT_GATTS_ROBUST_CACHING_ENABLED=y" in sdkconfig_defaults
assert "CONFIG_BT_GATTS_ROBUST_CACHING_ENABLED=y" in release_defaults

# Auto/mixed cold boot must finish BLE/NVS initialization before TinyUSB is
# exposed to an already-connected Windows host.  This is a startup ordering
# contract, not a timing delay.
transport_start = app_main[
    app_main.index("esp_err_t apply_transport_mode(") :
    app_main.index("void set_power_profile(")
]
mixed_start = transport_start[transport_start.index("// Initialize BLE/NVS") :]
assert mixed_start.index("codex_ble_start(transport_hooks)") < mixed_start.index(
    "codex_usb_start(transport_hooks, true)"
)
