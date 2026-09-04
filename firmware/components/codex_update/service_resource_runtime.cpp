#include "codex/service_resource_runtime.h"

namespace codex {

bool prepare_service_resources(const ServicePackSnapshot& snapshot,
                               ServiceResourceReader reader, void* context,
                               PreparedServiceResources& prepared) {
  prepared = {};
  if (reader == nullptr || snapshot.resource_count == 0 ||
      snapshot.resource_count > kServicePackMaximumResources) {
    return false;
  }
  unsigned int consumed_mask = 0;
  for (unsigned int index = 0; index < snapshot.resource_count; ++index) {
    const ServiceResourceDescriptor& resource = snapshot.resources[index];
    const unsigned int bit =
        1U << (static_cast<unsigned int>(resource.type) - 1U);
    if ((snapshot.reload_mask & bit) == 0) return false;
    switch (resource.type) {
      case ServiceResourceType::ProtocolPolicy: {
        if (prepared.has_protocol_policy ||
            resource.size != kCompatibilityPackWireSize) {
          return false;
        }
        std::uint8_t wire[kCompatibilityPackWireSize]{};
        if (!reader(resource.offset, wire, sizeof(wire), context)) {
          return false;
        }
        const CompatibilityPackResult parsed =
            parse_compatibility_pack(wire, sizeof(wire));
        if (!parsed.ok) return false;
        prepared.protocol_policy = parsed.snapshot;
        prepared.has_protocol_policy = true;
        consumed_mask |= bit;
        break;
      }
      case ServiceResourceType::ConfigSchema:
      case ServiceResourceType::AudioPreset:
      case ServiceResourceType::UiResource:
        // Their bounded wire formats and runtime consumers are deliberately
        // not claimed by this firmware version.
        return false;
    }
  }
  return consumed_mask == snapshot.reload_mask &&
         prepared.has_protocol_policy;
}

void activate_service_resources(const PreparedServiceResources& prepared) {
  if (prepared.has_protocol_policy) {
    activate_compatibility_snapshot(prepared.protocol_policy);
  }
}

}  // namespace codex
