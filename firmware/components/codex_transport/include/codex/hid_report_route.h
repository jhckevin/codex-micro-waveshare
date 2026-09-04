#pragma once

namespace codex {

struct HidReportRoute {
  unsigned char report_id{0};
  const unsigned char* data{nullptr};
  unsigned short size{0};
};

[[nodiscard]] constexpr bool hid_input_is_critical(unsigned char) {
  return true;
}

[[nodiscard]] constexpr bool hid_input_enqueue_to_front(unsigned char) {
  return false;
}

[[nodiscard]] constexpr HidReportRoute route_hid_output_report(
    unsigned char callback_report_id, const unsigned char* buffer,
    unsigned short size) {
  if (callback_report_id != 0) {
    return {callback_report_id, buffer, size};
  }
  if (buffer == nullptr || size == 0) return {};
  // TinyUSB uses report_id=0 for interrupt OUT and leaves the actual report
  // ID in byte zero.  Route the ID but strip it from the report body so both
  // interrupt OUT (Windows/node-hid) and control SET_REPORT share one parser.
  return {buffer[0], buffer + 1, static_cast<unsigned short>(size - 1)};
}

}  // namespace codex
