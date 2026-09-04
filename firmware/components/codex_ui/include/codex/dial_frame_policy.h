#pragma once

namespace codex {

constexpr unsigned int kDialFrameCount = 24;

[[nodiscard]] constexpr unsigned int dial_frame_index(float degrees) {
  while (degrees < 0.0F) degrees += 360.0F;
  while (degrees >= 360.0F) degrees -= 360.0F;
  const unsigned int rounded =
      static_cast<unsigned int>((degrees + 7.5F) / 15.0F);
  return rounded % kDialFrameCount;
}

}  // namespace codex
