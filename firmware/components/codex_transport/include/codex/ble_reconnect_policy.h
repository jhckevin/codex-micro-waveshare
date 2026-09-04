#pragma once

#include "codex/device_state.h"

namespace codex {

struct BleReconnectTarget {
  bool directed{};
  unsigned char address[6]{};
  unsigned char address_type{};
};

// A bonded HID peripheral cannot initiate a BLE connection itself. Directed
// low-duty advertising continuously invites the active bonded central to
// reopen the link when it returns into range. Pairing remains discoverable.
[[nodiscard]] constexpr BleReconnectTarget ble_reconnect_target(
    unsigned char active_slot, const BlePeerState peers[3], bool pairing) {
  BleReconnectTarget target{};
  if (pairing || active_slot < 1U || active_slot > 3U) return target;
  const BlePeerState& peer = peers[active_slot - 1U];
  if (!peer.bonded) return target;
  target.directed = true;
  target.address_type = peer.address_type;
  for (unsigned int index = 0; index < 6U; ++index) {
    target.address[index] = peer.address[index];
  }
  return target;
}

}  // namespace codex
