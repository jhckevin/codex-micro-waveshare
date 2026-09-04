#include "codex/touch_contact_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;

  TouchContactState contact{};
  TouchContactUpdate update =
      update_touch_contact(contact, TouchSampleKind::Contacts);
  require(update.state.pressed);
  require(!update.emit_release);
  require(update.valid_frame);

  update = update_touch_contact(update.state, TouchSampleKind::ReadFailure);
  require(update.state.pressed);
  require(!update.emit_release);
  require(!update.valid_frame);
  require(update.state.consecutive_read_failures == 1);

  update = update_touch_contact(update.state, TouchSampleKind::ReadFailure);
  require(update.state.pressed);
  require(!update.emit_release);
  require(update.state.consecutive_read_failures == 2);

  update = update_touch_contact(update.state, TouchSampleKind::Contacts);
  require(update.state.pressed);
  require(!update.emit_release);
  require(update.state.consecutive_read_failures == 0);

  update = update_touch_contact(update.state, TouchSampleKind::Empty);
  require(!update.state.pressed);
  require(update.emit_release);
  require(update.valid_frame);

  update = update_touch_contact(update.state, TouchSampleKind::Empty);
  require(!update.emit_release);

  ExitProcess(failures);
}
