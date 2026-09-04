# Backlight verification — 2026-09-04

Status: hardware-control fix flashed; optical dark-room acceptance still pending.

## Preservation
- Original source repository was not modified.
- Pre-change source checkpoint: remote backups/open-source-before-backlight-hold-20260904.tar.gz.
- Device ota_0 partition backed up locally (5,242,880 bytes), SHA256 3bd582b2cb52a83581e61b07bf2ab0b799176b8a082afba5a5916a7d7bda1732.
- New app SHA256 433085a6076d760eeb7a2715084f2d17557f0e665c07a9933b3b7a6c653bba45.
- Only app at 0x20000 was written. NVS, bonds, bootloader and eFuse were not written.
- Secure Boot and Flash Encryption were read as disabled; pre-existing HMAC key purpose remains.

## Changes
- Serialize brightness/PWM, hard-off and wake with a mutex; no per-frame processing added.
- Stop active-low LEDC, detach its output via GPIO configuration, set GPIO4 HIGH and hold that single pad.
- Read back GPIO4 and log errors instead of silently ignoring hard-off failures.
- Release hold explicitly on wake and initialize a known HIGH state before releasing retained hold after reset.
- Preserve historical LVGL 9.2 lv_init.c double-deinit fix in a versioned source override. It had existed only in the old managed-components tree.

## Verified
- Remote ESP-IDF build successful; image has roughly 45% free app partition capacity.
- Host test runner exit 0 (71 PASS-labelled tests plus contract checks).
- Flash hash verified.
- Two manual standby/wake cycles: GPIO4=1 hold=yes result=ESP_OK; BLE ready and codex_ready remained true.
- Automatic standby after existing timeout: same hard-off log at uptime 98631 ms; snapshot power=1 at 109495 ms, BLE ready and codex_ready true.
- Sampled metrics: drops=0, slow_frames=0, timing_faults=0, black_faults=0. Display unstable counter was 6; this is not an optical tear-free guarantee.

## Not verified
- Night-time visible glow, analog leakage, actual backlight current and external optical output.
- Ultra full regression, extended stability, macOS, multi-host BLE.
- Do not label the residual-glow report resolved until physical dark-room confirmation.
