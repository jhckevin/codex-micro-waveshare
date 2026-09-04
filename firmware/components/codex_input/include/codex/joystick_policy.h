#pragma once

namespace codex {

[[nodiscard]] constexpr float scale_classic_joystick(
    float distance, unsigned char sensitivity_percent) {
  if (distance <= 0.0F) return 0.0F;
  unsigned int sensitivity = sensitivity_percent;
  if (sensitivity < 50U) sensitivity = 50U;
  if (sensitivity > 200U) sensitivity = 200U;
  const float scaled =
      distance * static_cast<float>(sensitivity) / 100.0F;
  return scaled < 1.0F ? scaled : 1.0F;
}

}  // namespace codex
