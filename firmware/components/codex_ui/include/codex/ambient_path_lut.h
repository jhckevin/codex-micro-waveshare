#pragma once

namespace codex {

inline constexpr unsigned int kAmbientPathSampleCount = 128;
inline constexpr unsigned int kAmbientSnakeMoverCount = 5;
struct AmbientPathPoint {
  short x;
  short y;
  bool horizontal;
};
inline constexpr AmbientPathPoint kAmbientPath[kAmbientPathSampleCount] = {
    {55, 11, true},
    {71, 11, true},
    {87, 11, true},
    {103, 11, true},
    {119, 11, true},
    {135, 11, true},
    {152, 11, true},
    {168, 11, true},
    {184, 11, true},
    {200, 11, true},
    {216, 11, true},
    {232, 11, true},
    {248, 11, true},
    {264, 11, true},
    {280, 11, true},
    {296, 11, true},
    {312, 11, true},
    {328, 11, true},
    {345, 11, true},
    {361, 11, true},
    {377, 11, true},
    {393, 11, true},
    {409, 11, true},
    {425, 11, true},
    {434, 12, true},
    {442, 14, true},
    {449, 18, true},
    {456, 24, false},
    {462, 31, false},
    {466, 38, false},
    {468, 46, false},
    {469, 55, false},
    {469, 70, false},
    {469, 86, false},
    {469, 101, false},
    {469, 117, false},
    {469, 132, false},
    {469, 148, false},
    {469, 163, false},
    {469, 178, false},
    {469, 194, false},
    {469, 209, false},
    {469, 225, false},
    {469, 240, false},
    {469, 255, false},
    {469, 271, false},
    {469, 286, false},
    {469, 302, false},
    {469, 317, false},
    {469, 332, false},
    {469, 348, false},
    {469, 363, false},
    {469, 379, false},
    {469, 394, false},
    {469, 410, false},
    {469, 425, false},
    {468, 434, false},
    {466, 442, false},
    {462, 449, false},
    {456, 456, false},
    {449, 462, true},
    {442, 466, true},
    {434, 468, true},
    {425, 469, true},
    {410, 469, true},
    {394, 469, true},
    {379, 469, true},
    {363, 469, true},
    {348, 469, true},
    {332, 469, true},
    {317, 469, true},
    {302, 469, true},
    {286, 469, true},
    {271, 469, true},
    {255, 469, true},
    {240, 469, true},
    {225, 469, true},
    {209, 469, true},
    {194, 469, true},
    {178, 469, true},
    {163, 469, true},
    {148, 469, true},
    {132, 469, true},
    {117, 469, true},
    {101, 469, true},
    {86, 469, true},
    {70, 469, true},
    {55, 469, true},
    {46, 468, true},
    {38, 466, true},
    {31, 462, true},
    {24, 456, true},
    {18, 449, false},
    {14, 442, false},
    {12, 434, false},
    {11, 425, false},
    {11, 410, false},
    {11, 394, false},
    {11, 379, false},
    {11, 363, false},
    {11, 348, false},
    {11, 332, false},
    {11, 317, false},
    {11, 302, false},
    {11, 286, false},
    {11, 271, false},
    {11, 255, false},
    {11, 240, false},
    {11, 225, false},
    {11, 209, false},
    {11, 194, false},
    {11, 178, false},
    {11, 163, false},
    {11, 148, false},
    {11, 132, false},
    {11, 117, false},
    {11, 101, false},
    {11, 86, false},
    {11, 70, false},
    {11, 55, false},
    {12, 46, false},
    {14, 38, false},
    {18, 31, false},
    {24, 24, false},
    {31, 18, true},
    {38, 14, true},
    {46, 12, true},
    {55, 11, true},
};

[[nodiscard]] constexpr AmbientPathPoint ambient_path_point(
    unsigned int phase) {
  return kAmbientPath[phase & (kAmbientPathSampleCount - 1U)];
}

[[nodiscard]] constexpr unsigned int ambient_tail_phase(
    unsigned int head_phase, unsigned int tail_index) {
  return (head_phase + kAmbientPathSampleCount - tail_index * 4U) &
         (kAmbientPathSampleCount - 1U);
}

[[nodiscard]] constexpr AmbientPathPoint ambient_path_interpolated(
    unsigned int phase, unsigned int fraction_256) {
  const AmbientPathPoint current = ambient_path_point(phase);
  const AmbientPathPoint next = ambient_path_point(phase + 1U);
  if (fraction_256 > 255U) fraction_256 = 255U;
  const auto interpolate = [fraction_256](short from, short to) {
    return static_cast<short>(
        static_cast<int>(from) +
        ((static_cast<int>(to) - static_cast<int>(from)) *
             static_cast<int>(fraction_256) +
         128) /
            256);
  };
  return {
      interpolate(current.x, next.x),
      interpolate(current.y, next.y),
      fraction_256 < 128U ? current.horizontal : next.horizontal,
  };
}

}  // namespace codex
