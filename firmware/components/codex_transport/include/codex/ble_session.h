#pragma once

namespace codex {

constexpr unsigned int kPreAuthReportMaxBytes = 63;
// One complete 4096-byte configuration message can occupy 68 HID fragments.
// Keep the queue bounded, but large enough that authentication timing cannot
// truncate a valid maximum-size request.
constexpr unsigned int kPreAuthReportCapacity = 72;

enum class BleSessionPhase : unsigned char {
  Down,
  LinkOpen,
  Authorized,
  Ready,
};

enum class PreAuthPushResult : unsigned char {
  Queued,
  Overflow,
  Invalid,
};

enum class BleSlotSwitchAction : unsigned char {
  None,
  KeepAdvertising,
  DisconnectPeer,
};

struct PreAuthReport {
  unsigned char report_id{};
  unsigned char length{};
  unsigned char data[kPreAuthReportMaxBytes]{};
};

struct PreAuthReportQueue {
  PreAuthReport reports[kPreAuthReportCapacity]{};
  unsigned char head{};
  unsigned char count{};
  unsigned int overflow_count{};
};

struct BleSession {
  BleSessionPhase phase{BleSessionPhase::Down};
  bool host_output_seen{};
  PreAuthReportQueue pre_auth{};
};

inline void clear_pre_auth_reports(PreAuthReportQueue& queue) {
  queue.head = 0;
  queue.count = 0;
}

[[nodiscard]] inline PreAuthPushResult push_pre_auth_report(
    PreAuthReportQueue& queue, unsigned char report_id,
    const unsigned char* data, unsigned int length) {
  if (data == nullptr || length == 0 || length > kPreAuthReportMaxBytes) {
    return PreAuthPushResult::Invalid;
  }
  if (queue.count >= kPreAuthReportCapacity) {
    ++queue.overflow_count;
    return PreAuthPushResult::Overflow;
  }
  const unsigned int tail =
      (static_cast<unsigned int>(queue.head) + queue.count) %
      kPreAuthReportCapacity;
  PreAuthReport& report = queue.reports[tail];
  report.report_id = report_id;
  report.length = static_cast<unsigned char>(length);
  for (unsigned int index = 0; index < length; ++index) {
    report.data[index] = data[index];
  }
  ++queue.count;
  return PreAuthPushResult::Queued;
}

[[nodiscard]] inline bool pop_pre_auth_report(PreAuthReportQueue& queue,
                                              PreAuthReport* output) {
  if (output == nullptr || queue.count == 0) return false;
  *output = queue.reports[queue.head];
  queue.head = static_cast<unsigned char>(
      (static_cast<unsigned int>(queue.head) + 1U) %
      kPreAuthReportCapacity);
  --queue.count;
  return true;
}

inline void ble_link_opened(BleSession& session) {
  session.phase = BleSessionPhase::LinkOpen;
  session.host_output_seen = false;
  clear_pre_auth_reports(session.pre_auth);
}

inline void ble_host_output_seen(BleSession& session) {
  session.host_output_seen = true;
  if (session.phase == BleSessionPhase::Authorized) {
    session.phase = BleSessionPhase::Ready;
  }
}

inline void ble_authorization_succeeded(BleSession& session) {
  if (session.phase == BleSessionPhase::Down) return;
  session.phase = session.host_output_seen ? BleSessionPhase::Ready
                                           : BleSessionPhase::Authorized;
}

inline void ble_authorization_failed(BleSession& session) {
  session.phase = BleSessionPhase::Down;
  session.host_output_seen = false;
  clear_pre_auth_reports(session.pre_auth);
}

inline void ble_disconnected(BleSession& session) {
  ble_authorization_failed(session);
}

[[nodiscard]] constexpr bool ble_session_authorized(
    const BleSession& session) {
  return session.phase == BleSessionPhase::Authorized ||
         session.phase == BleSessionPhase::Ready;
}

[[nodiscard]] constexpr bool ble_session_ready(const BleSession& session) {
  return session.phase == BleSessionPhase::Ready;
}

[[nodiscard]] constexpr bool should_restore_bonded_cccs(
    const BleSession& session, bool already_restored) {
  // A returning bonded central expects its notification subscriptions to be
  // live as soon as authentication succeeds. Waiting for HID Output creates a
  // reconnect deadlock on Windows: each side waits for the other to speak.
  return !already_restored && ble_session_authorized(session);
}

[[nodiscard]] constexpr bool ble_session_blocks_auto_shutdown(
    const BleSession& session) {
  return ble_session_authorized(session);
}

[[nodiscard]] constexpr BleSlotSwitchAction ble_slot_switch_action(
    bool stack_started, bool peer_connected) {
  if (!stack_started) return BleSlotSwitchAction::None;
  return peer_connected ? BleSlotSwitchAction::DisconnectPeer
                        : BleSlotSwitchAction::KeepAdvertising;
}

}  // namespace codex
