#include "codex/hid_report_route.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
int failures = 0;
void require(bool condition) {
  if (!condition) ++failures;
}
}  // namespace

extern "C" void mainCRTStartup() {
  unsigned char host_report[64]{};
  host_report[0] = 6;
  host_report[1] = 2;
  host_report[2] = 17;

  const codex::HidReportRoute interrupt_out =
      codex::route_hid_output_report(0, host_report, sizeof(host_report));
  require(interrupt_out.report_id == 6);
  require(interrupt_out.data == host_report + 1);
  require(interrupt_out.size == 63);
  require(interrupt_out.data[0] == 2);

  const codex::HidReportRoute control_out =
      codex::route_hid_output_report(7, host_report + 1, 63);
  require(control_out.report_id == 7);
  require(control_out.data == host_report + 1);
  require(control_out.size == 63);

  const codex::HidReportRoute empty =
      codex::route_hid_output_report(0, nullptr, 0);
  require(empty.report_id == 0);
  require(empty.data == nullptr);
  require(empty.size == 0);

  require(codex::hid_input_is_critical(1));
  require(codex::hid_input_is_critical(0));
  require(!codex::hid_input_enqueue_to_front(1));
  require(!codex::hid_input_enqueue_to_front(0));

  ExitProcess(static_cast<unsigned long>(failures));
}
