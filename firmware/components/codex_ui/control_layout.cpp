#include "codex/control_layout.h"

namespace codex {

ControlLayout make_control_layout() {
  ControlLayout layout{};
  unsigned int n = 0;
  const auto add = [&layout, &n](ControlKind kind, unsigned char index,
                                 int x, int y, int width = 91) {
    layout.controls[n++] = {kind, index, {x, y, width, 91}};
  };

  add(ControlKind::Dial, 0, 45, 45);
  add(ControlKind::Agent, 0, 143, 45);
  add(ControlKind::Agent, 1, 241, 45);
  add(ControlKind::Joystick, 0, 339, 45);
  add(ControlKind::Agent, 2, 45, 143);
  add(ControlKind::Agent, 3, 143, 143);
  add(ControlKind::Agent, 4, 241, 143);
  add(ControlKind::Agent, 5, 339, 143);
  add(ControlKind::Command, 0, 45, 241);
  add(ControlKind::Command, 1, 143, 241);
  add(ControlKind::Command, 2, 241, 241);
  add(ControlKind::Command, 3, 339, 241);
  add(ControlKind::Capacitive, 0, 45, 339);
  add(ControlKind::Command, 4, 143, 339, 189);
  add(ControlKind::Command, 5, 339, 339);
  return layout;
}

bool hit_test(const ControlLayout& layout, Point point, ControlHit* output) {
  for (unsigned int index = 0; index < kControlCount; ++index) {
    const Rect& bounds = layout.controls[index].bounds;
    if (point.x >= bounds.x && point.x < bounds.x + bounds.width &&
        point.y >= bounds.y && point.y < bounds.y + bounds.height) {
      if (output != nullptr) *output = layout.controls[index];
      return true;
    }
  }
  return false;
}

}  // namespace codex
