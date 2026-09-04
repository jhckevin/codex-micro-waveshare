#pragma once

namespace codex {

constexpr unsigned int kDisplayFadeStepCount = 5U;
constexpr unsigned int kDisplayFadeStepDelayMs = 6U;

[[nodiscard]] constexpr unsigned char display_fade_level(
    unsigned char brightness, unsigned int step) {
  constexpr unsigned char kPercent[kDisplayFadeStepCount] = {
      100U, 72U, 44U, 18U, 0U};
  if (step >= kDisplayFadeStepCount) step = kDisplayFadeStepCount - 1U;
  return static_cast<unsigned char>(
      (static_cast<unsigned int>(brightness) * kPercent[step] + 50U) / 100U);
}

}  // namespace codex
