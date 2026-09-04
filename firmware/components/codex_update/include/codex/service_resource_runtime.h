#pragma once

#include <cstddef>
#include <cstdint>

#include "codex/compatibility_pack.h"
#include "codex/service_pack.h"

namespace codex {

using ServiceResourceReader = bool (*)(std::uint32_t offset,
                                       std::uint8_t* output,
                                       std::size_t size,
                                       void* context);

struct PreparedServiceResources {
  CompatibilitySnapshot protocol_policy{};
  bool has_protocol_policy{};
};

// Only resource types with a compiled, bounded consumer are accepted. This
// prevents a signed pack from reporting success while silently storing an
// unsupported resource.
[[nodiscard]] bool prepare_service_resources(
    const ServicePackSnapshot& snapshot, ServiceResourceReader reader,
    void* context, PreparedServiceResources& prepared);
void activate_service_resources(const PreparedServiceResources& prepared);

}  // namespace codex
