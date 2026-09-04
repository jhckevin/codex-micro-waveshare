#pragma once

namespace codex {

constexpr int kSurfaceWidth = 480;
constexpr int kSurfaceHeight = 480;
constexpr unsigned int kControlCount = 15;

struct Point { int x; int y; };
struct Rect { int x; int y; int width; int height; };

enum class ControlKind : unsigned char {
  Dial,
  Agent,
  Joystick,
  Command,
  Capacitive,
};

struct ControlHit {
  ControlKind kind{ControlKind::Dial};
  unsigned char index{0};
  Rect bounds{};
};

struct ControlLayout { ControlHit controls[kControlCount]{}; };

[[nodiscard]] ControlLayout make_control_layout();
[[nodiscard]] bool hit_test(const ControlLayout& layout, Point point,
                            ControlHit* output);

}  // namespace codex
