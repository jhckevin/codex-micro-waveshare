#pragma once

#include "codex/device_state.h"

namespace codex {

constexpr unsigned long long kWrongSlotAdvertisingBackoffUs = 1000000ULL;

enum class BleRejectionPhase : unsigned char {
  Idle,
  DisconnectPending,
  AwaitingDisconnect,
  AdvertisingBackoff,
};

enum class BleRejectionAction : unsigned char {
  None,
  DeferDisconnect,
  StartAdvertisingBackoff,
  RestartAdvertising,
};

struct BleRejectionState {
  BleRejectionPhase phase{BleRejectionPhase::Idle};
};

struct BlePeerResolution {
  bool allowed{};
  unsigned char slot{};
  bool recovered_slot{};
};

[[nodiscard]] constexpr bool ble_peer_address_equal(
    const unsigned char left[6], const unsigned char right[6]) {
  for (unsigned int index = 0; index < 6; ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

[[nodiscard]] constexpr BlePeerResolution resolve_ble_peer(
    unsigned char active_slot, const BlePeerState peers[3],
    const unsigned char address[6], bool pairing_new_peer) {
  if (active_slot < 1 || active_slot > 3 || address == nullptr) return {};
  const unsigned int active = active_slot - 1U;
  if (peers[active].bonded) {
    return {
        .allowed = ble_peer_address_equal(peers[active].address, address),
        .slot = active_slot,
        .recovered_slot = false,
    };
  }
  (void)pairing_new_peer;
  for (unsigned int index = 0; index < 3; ++index) {
    if (index != active && peers[index].bonded &&
        ble_peer_address_equal(peers[index].address, address)) {
      // Slot selection is an explicit user choice. A known peer from another
      // slot must never migrate into an empty active slot automatically.
      return {};
    }
  }
  return {
      .allowed = true,
      .slot = active_slot,
      .recovered_slot = false,
  };
}

[[nodiscard]] constexpr BleRejectionAction note_wrong_slot_peer(
    BleRejectionState& state) {
  if (state.phase != BleRejectionPhase::Idle) {
    return BleRejectionAction::None;
  }
  state.phase = BleRejectionPhase::DisconnectPending;
  return BleRejectionAction::DeferDisconnect;
}

[[nodiscard]] constexpr bool mark_deferred_disconnect_started(
    BleRejectionState& state) {
  if (state.phase != BleRejectionPhase::DisconnectPending) return false;
  state.phase = BleRejectionPhase::AwaitingDisconnect;
  return true;
}

[[nodiscard]] constexpr BleRejectionAction note_ble_disconnected(
    BleRejectionState& state) {
  if (state.phase == BleRejectionPhase::DisconnectPending ||
      state.phase == BleRejectionPhase::AwaitingDisconnect) {
    state.phase = BleRejectionPhase::AdvertisingBackoff;
    return BleRejectionAction::StartAdvertisingBackoff;
  }
  state.phase = BleRejectionPhase::Idle;
  return BleRejectionAction::RestartAdvertising;
}

[[nodiscard]] constexpr BleRejectionAction note_advertising_backoff_elapsed(
    BleRejectionState& state) {
  if (state.phase != BleRejectionPhase::AdvertisingBackoff) {
    return BleRejectionAction::None;
  }
  state.phase = BleRejectionPhase::Idle;
  return BleRejectionAction::RestartAdvertising;
}

inline void reset_ble_rejection(BleRejectionState& state) {
  state.phase = BleRejectionPhase::Idle;
}

}  // namespace codex
