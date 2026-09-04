# Development HIL

This control plane is compiled only when `CONFIG_CODEX_DEVELOPMENT_HIL=y`.
Production builds add `sdkconfig.release.defaults`, which removes the parser,
UART task, virtual pointer and display hooks from the firmware image.

UART0 remains on the independent CH343 `USB TO UART` connector. Native USB
remains available to Codex HID/CDC and BOOT/GPIO0 remains the ROM recovery path.

Examples:

```text
@hil ping
@hil snapshot
@hil metrics
@hil trace
@hil touch down 0 237 384
@hil touch up 0
@hil tap 188 90
@hil key down mic
@hil key up mic
@hil power
@hil ultra
@hil joystick 90 0.80
@hil encoder 1
@hil ble clear 1
@hil lighting stress on
@hil screen crc
@hil screen read 448 448 32 32
@hil app connect
@hil app heartbeat
@hil app disconnect
@hil layer 2
@hil routing layers 3
@hil routing layer1 4 2
@hil routing passthrough 2 agent 0x02
@hil routing snapshot
@hil lighting app agent 6 4 1 0.5 0 0x304ffe
```

Private input is reported as one flat control address, such as `agent:6`.
Layer selection is resolved locally in firmware and is never added to the App
event payload. `routing passthrough` masks are limited to six bits for Agent
and Command groups and one bit for Encoder and Joystick.

Run the automated board suite from an isolated Python environment that already
contains `pyserial`:

```powershell
python tools/hil/hil_runner.py --port COM5 --stress-seconds 30
```

The continuous monitor samples one 160 x 160 screen tile on a sparse 8 x 8
grid every 32 ms and derives the tile fingerprint from those same 64 reads.
It deliberately avoids full-tile CRC scans because sustained PSRAM reads can
starve the RGB bounce-buffer refill and create physical scanout corruption.
Flush and VSYNC callbacks only publish atomics. A correct framebuffer plus a physical
scanout/panel-only artifact still requires a synchronized camera to prove the
optical output; the serial evidence provides timestamps for that extension.
