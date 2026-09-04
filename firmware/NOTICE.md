# Notices and Disclaimers

## Copyright and license

Copyright (c) 2026 imliubo.
Copyright (c) 2026 Jhckevin for subsequent modifications and original
Codex Micro Control components.

Unless a file states otherwise, the original source code and documentation in
this repository are licensed under the [MIT License](LICENSE).

The MIT License applies only to material owned by this project's copyright
holder. It does not grant rights to third-party trademarks, product names,
logos, hardware designs, firmware, protocols, libraries, or other intellectual
property.

## Independent project and trademarks

This is an independent, unofficial compatibility project. It is not affiliated
with, authorized by, endorsed by, sponsored by, or supported by OpenAI, Work
Louder, M5Stack, or any of their affiliates.

OpenAI, ChatGPT, and Codex are trademarks or registered trademarks of OpenAI.
Work Louder and Codex Micro product branding belong to their respective owners.
M5Stack and Core2 are trademarks or product names of M5Stack. All other names
and marks are the property of their respective owners.

Names and marks are used only to identify compatibility and the intended use of
the software. Their use does not imply an official relationship.

## Protocol and compatibility disclaimer

This firmware implements behavior observed from an undocumented vendor HID
interface. The BLE device name, manufacturer string, vendor ID, product ID,
usage page, report ID, key IDs, and RPC method names are used only so a compatible
host can recognize and communicate with the device. These identifiers are not
assigned to this project and must not be interpreted as proof of certification,
ownership, authenticity, or official-device status.

No representation is made that the implementation is complete, accurate,
secure, permanently compatible, or suitable for production use. OpenAI, Work
Louder, ChatGPT Desktop, macOS, ESP32 libraries, or M5Stack hardware may change
at any time and may cause partial or complete loss of functionality.

Do not contact OpenAI, Work Louder, or M5Stack for support with this firmware.
Use this repository's issue tracker for community support.

## Hardware, security, and data disclaimer

Flashing third-party firmware can erase existing software, settings, or data and
can leave hardware temporarily unusable. Verify the target board and keep a
known-good recovery path before flashing. You assume all risk for hardware
damage, data loss, radio behavior, battery use, security, and regulatory
compliance.

BLE pairing uses a "Just Works" flow without passkey authentication. Use the
device only in a trusted environment and remove stale pairings from both the
host and device when appropriate.

This firmware does not connect directly to OpenAI and does not stream the Core2
microphone. It does send button events, directional actions, and device status
to the paired computer. The paired operating system and ChatGPT Desktop control
how those events, the computer microphone, and task data are processed. Review
their privacy and security settings separately.

## No warranty

THE SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND. TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE AUTHORS AND COPYRIGHT HOLDERS
ARE NOT LIABLE FOR ANY CLAIM, DAMAGE, LOSS, OR OTHER LIABILITY ARISING FROM USE,
MODIFICATION, FLASHING, PAIRING, DISTRIBUTION, OR INABILITY TO USE THIS PROJECT.
