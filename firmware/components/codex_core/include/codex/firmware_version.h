#pragma once

#define CODEX_PUBLIC_FIRMWARE_VERSION "1.0.1.1"

namespace codex {

inline constexpr char kPublicFirmwareVersion[] =
    CODEX_PUBLIC_FIRMWARE_VERSION;
inline constexpr char kFirmwareSettingsLabel[] =
    "Firmware " CODEX_PUBLIC_FIRMWARE_VERSION;

}  // namespace codex
