#pragma once

#include <cstddef>
#include <cstdint>

#include "codex/update_manifest.h"

namespace codex {

void make_update_device_id(
    const std::uint8_t base_mac[6],
    std::uint8_t output[kUpdateDeviceIdSize]);
[[nodiscard]] bool derive_update_kek_from_efuse(
    const std::uint8_t device_id[kUpdateDeviceIdSize],
    std::uint8_t output[kUpdateDigestSize]);
[[nodiscard]] bool make_update_attestation(
    const std::uint8_t device_id[kUpdateDeviceIdSize],
    const std::uint8_t challenge[kUpdateDigestSize],
    std::uint8_t output[kUpdateDigestSize]);

[[nodiscard]] const std::uint8_t* update_public_key_der();
[[nodiscard]] std::size_t update_public_key_der_size();

}  // namespace codex
