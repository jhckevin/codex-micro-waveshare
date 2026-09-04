#include "codex/lighting_mailbox.h"

namespace codex {
namespace {

bool slot_for_event(const DeviceEvent& event, unsigned int* slot) {
  if (slot == nullptr) return false;
  switch (event.type) {
    case EventType::AgentLightingChanged:
      if (event.payload.agent_lighting.index >= kAgentCount) return false;
      *slot = event.payload.agent_lighting.index;
      return true;
    case EventType::AmbientLightingChanged:
      *slot = kAgentCount;
      return true;
    case EventType::KeysLightingChanged:
      *slot = kAgentCount + 1U;
      return true;
    case EventType::TemporaryLightingChanged:
      *slot = kAgentCount + 2U;
      return true;
    default:
      return false;
  }
}

}  // namespace

bool publish_lighting_event(LightingMailbox& mailbox,
                            const DeviceEvent& event) {
  unsigned int index = 0;
  if (!slot_for_event(event, &index)) return false;
  LightingMailboxSlot& slot = mailbox.slots[index];
  if (slot.valid && slot.version != slot.consumed_version) {
    ++mailbox.coalesced_count;
  }
  slot.event = event;
  ++slot.version;
  if (slot.version == 0) ++slot.version;
  slot.valid = true;
  return true;
}

MailboxRead consume_lighting_mailbox(LightingMailbox& mailbox,
                                     DeviceEvent* output,
                                     unsigned int capacity) {
  MailboxRead read{.coalesced = mailbox.coalesced_count};
  if (output == nullptr || capacity == 0) return read;
  for (unsigned int index = 0;
       index < kLightingMailboxSlotCount && read.count < capacity; ++index) {
    LightingMailboxSlot& slot = mailbox.slots[index];
    if (!slot.valid || slot.version == slot.consumed_version) continue;
    output[read.count++] = slot.event;
    slot.consumed_version = slot.version;
  }
  return read;
}

}  // namespace codex
