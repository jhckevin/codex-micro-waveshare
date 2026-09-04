#pragma once

#include "codex/update_session.h"

namespace codex {

// Multiplexes verified declarative packages to the inactive compatibility
// partition and complete images to the inactive OTA application slot.
[[nodiscard]] UpdateStorage make_device_update_storage();
[[nodiscard]] bool load_active_compatibility_pack();
[[nodiscard]] bool load_active_service_pack();
[[nodiscard]] std::uint64_t stored_update_release_sequence(
    UpdatePackageClass package_class);

}  // namespace codex
