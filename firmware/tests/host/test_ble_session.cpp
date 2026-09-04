#include "codex/ble_session.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  BleSession session{};
  ble_link_opened(session);
  require(session.phase == BleSessionPhase::LinkOpen);
  require(!ble_session_authorized(session));
  require(!ble_session_ready(session));
  require(!ble_session_blocks_auto_shutdown(session));

  const unsigned char first[] = {2, 3, 'o', 'n', 'e'};
  const unsigned char second[] = {2, 3, 't', 'w', 'o'};
  require(push_pre_auth_report(session.pre_auth, 6, first, sizeof(first)) ==
          PreAuthPushResult::Queued);
  require(push_pre_auth_report(session.pre_auth, 7, second, sizeof(second)) ==
          PreAuthPushResult::Queued);
  ble_host_output_seen(session);
  require(session.phase == BleSessionPhase::LinkOpen);

  ble_authorization_succeeded(session);
  require(session.phase == BleSessionPhase::Ready);
  require(ble_session_authorized(session));
  require(ble_session_ready(session));
  require(ble_session_blocks_auto_shutdown(session));

  PreAuthReport report{};
  require(pop_pre_auth_report(session.pre_auth, &report));
  require(report.report_id == 6 && report.length == sizeof(first));
  require(report.data[2] == 'o');
  require(pop_pre_auth_report(session.pre_auth, &report));
  require(report.report_id == 7 && report.data[2] == 't');
  require(!pop_pre_auth_report(session.pre_auth, &report));

  ble_disconnected(session);
  require(session.phase == BleSessionPhase::Down);
  require(session.pre_auth.count == 0);

  ble_link_opened(session);
  unsigned char packet[kPreAuthReportMaxBytes]{};
  for (unsigned int index = 0; index < kPreAuthReportCapacity; ++index) {
    require(push_pre_auth_report(
                session.pre_auth, 6, packet, sizeof(packet)) ==
            PreAuthPushResult::Queued);
  }
  require(push_pre_auth_report(session.pre_auth, 6, packet, sizeof(packet)) ==
          PreAuthPushResult::Overflow);
  require(session.pre_auth.overflow_count == 1);
  ble_authorization_failed(session);
  require(session.phase == BleSessionPhase::Down);
  require(session.pre_auth.count == 0);

  require(!should_restore_bonded_cccs(session, false));
  ble_link_opened(session);
  require(!should_restore_bonded_cccs(session, false));
  ble_authorization_succeeded(session);
  require(should_restore_bonded_cccs(session, false));
  require(!should_restore_bonded_cccs(session, true));
  ble_host_output_seen(session);
  require(session.phase == BleSessionPhase::Ready);
  require(ble_slot_switch_action(false, false) == BleSlotSwitchAction::None);
  require(ble_slot_switch_action(true, false) ==
          BleSlotSwitchAction::KeepAdvertising);
  require(ble_slot_switch_action(true, true) ==
          BleSlotSwitchAction::DisconnectPeer);
  ExitProcess(failures);
}
