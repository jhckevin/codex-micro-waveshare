#pragma once

#include "codex/device_event.h"

namespace codex {

inline constexpr unsigned int kLightingMailboxSlotCount = kAgentCount + 3U;

struct LightingMailboxSlot {
  DeviceEvent event{};
  unsigned int version{0};
  unsigned int consumed_version{0};
  bool valid{false};
};

struct LightingMailbox {
  LightingMailboxSlot slots[kLightingMailboxSlotCount]{};
  unsigned int coalesced_count{0};
};

struct MailboxRead {
  unsigned int count{0};
  unsigned int coalesced{0};
};

[[nodiscard]] bool publish_lighting_event(LightingMailbox& mailbox,
                                          const DeviceEvent& event);
[[nodiscard]] MailboxRead consume_lighting_mailbox(
    LightingMailbox& mailbox, DeviceEvent* output, unsigned int capacity);

}  // namespace codex
