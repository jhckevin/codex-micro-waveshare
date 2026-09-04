#pragma once

#include "codex/update_session.h"

namespace codex {

// Provides an UpdateStorage backend that writes only to the inactive ESP-IDF
// OTA slot. The boot partition is selected only after the complete raw image
// digest has been verified.
[[nodiscard]] UpdateStorage make_firmware_update_storage();

// Call once after normal hardware initialization. A newly booted OTA image is
// kept pending until the application-level self-test explicitly confirms it.
[[nodiscard]] bool firmware_update_pending_verification();
[[nodiscard]] bool confirm_running_firmware();
[[nodiscard]] bool reject_running_firmware_and_reboot();

}  // namespace codex
