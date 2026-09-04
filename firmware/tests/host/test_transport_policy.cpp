#include "codex/transport_policy.h"
#include "codex/ble_rejection_policy.h"
#include "codex/ble_reconnect_policy.h"
#include "codex/ble_session.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool value) { failures += value ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(choose_active_transport(TransportMode::Mixed, true, true) ==
          ActiveTransport::Usb);
  require(choose_active_transport(TransportMode::Mixed, false, true) ==
          ActiveTransport::Ble);
  require(choose_active_transport(TransportMode::Auto, true, true) ==
          ActiveTransport::Usb);
  require(!keep_ble_running(TransportMode::Auto, true, true));
  require(keep_ble_running(TransportMode::Auto, false, true));
  require(keep_ble_running(TransportMode::Mixed, true, true));
  require(!keep_ble_running(TransportMode::Ble, false, false));
  require(!keep_ble_running(TransportMode::Mixed, false, false));
  require(allow_auto_transport_handoff(PowerMode::Active));
  require(!allow_auto_transport_handoff(PowerMode::Screensaver));
  require(!allow_auto_transport_handoff(PowerMode::ProtectedBlack));
  require(!allow_auto_transport_handoff(PowerMode::ProtectedUnlock));
  require(!allow_auto_transport_handoff(PowerMode::PowerOffPending));
  require(choose_active_transport(TransportMode::Usb, false, true) ==
          ActiveTransport::None);
  require(transport_event_requires_order(EventType::TransportConnected));
  require(transport_event_requires_order(EventType::TransportDisconnected));
  require(transport_event_requires_order(EventType::BlePeerBonded));
  require(transport_event_requires_order(EventType::AppSessionHeartbeat));
  require(transport_event_requires_order(EventType::RoutingConfigChanged));
  require(!transport_event_requires_order(EventType::CodexRpcReceived));
  require(!transport_event_requires_order(EventType::AgentLightingChanged));
  require(quarantine_ble_input_channel(false, true, true));
  require(!quarantine_ble_input_channel(true, true, true));
  require(!quarantine_ble_input_channel(false, false, true));
  require(!quarantine_ble_input_channel(false, true, false));

  AutoTransportDebounce debounce{};
  require(!auto_transport_observation_stable(debounce, false, 100U));
  require(!auto_transport_observation_stable(
      debounce, false, 100U + kAutoTransportStableMs - 1U));
  require(auto_transport_observation_stable(
      debounce, false, 100U + kAutoTransportStableMs));
  require(!auto_transport_observation_stable(debounce, true, 2000U));
  require(!auto_transport_observation_stable(
      debounce, true, 2000U + kAutoTransportStableMs - 1U));
  require(auto_transport_observation_stable(
      debounce, true, 2000U + kAutoTransportStableMs));

  BleRejectionState rejection{};
  require(note_wrong_slot_peer(rejection) ==
          BleRejectionAction::DeferDisconnect);
  require(note_wrong_slot_peer(rejection) == BleRejectionAction::None);
  require(mark_deferred_disconnect_started(rejection));
  require(note_ble_disconnected(rejection) ==
          BleRejectionAction::StartAdvertisingBackoff);
  require(note_wrong_slot_peer(rejection) == BleRejectionAction::None);
  require(note_advertising_backoff_elapsed(rejection) ==
          BleRejectionAction::RestartAdvertising);
  require(note_ble_disconnected(rejection) ==
          BleRejectionAction::RestartAdvertising);
  require(kWrongSlotAdvertisingBackoffUs >= 750000ULL);

  const unsigned char known_peer[6] = {0x84, 0xD1, 0xC1,
                                       0x7A, 0xDD, 0x2E};
  const unsigned char other_peer[6] = {1, 2, 3, 4, 5, 6};
  BlePeerState peers[3]{};
  for (unsigned int index = 0; index < 6; ++index) {
    peers[0].address[index] = known_peer[index];
  }
  peers[0].bonded = true;
  peers[0].address_type = 1;

  const BleReconnectTarget reconnect =
      ble_reconnect_target(1, peers, false);
  require(reconnect.directed && reconnect.address_type == 1);
  require(ble_peer_address_equal(reconnect.address, known_peer));
  require(!ble_reconnect_target(1, peers, true).directed);
  require(!ble_reconnect_target(2, peers, false).directed);
  require(!ble_reconnect_target(0, peers, false).directed);
  require(ble_slot_switch_action(true, false) ==
          BleSlotSwitchAction::KeepAdvertising);

  BlePeerResolution recovered =
      resolve_ble_peer(3, peers, known_peer, false);
  require(!recovered.allowed);
  BlePeerResolution pairing =
      resolve_ble_peer(3, peers, known_peer, true);
  require(!pairing.allowed);

  for (unsigned int index = 0; index < 6; ++index) {
    peers[2].address[index] = other_peer[index];
  }
  peers[2].bonded = true;
  BlePeerResolution occupied =
      resolve_ble_peer(3, peers, known_peer, false);
  require(!occupied.allowed);

  BlePeerResolution active =
      resolve_ble_peer(1, peers, known_peer, false);
  require(active.allowed && active.slot == 1 && !active.recovered_slot);

  BlePeerState empty_peers[3]{};
  BlePeerResolution fresh =
      resolve_ble_peer(3, empty_peers, other_peer, false);
  require(fresh.allowed && fresh.slot == 3 && !fresh.recovered_slot);
  ExitProcess(failures);
}
