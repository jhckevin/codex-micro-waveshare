#pragma once

namespace codex {

inline constexpr unsigned char kLayerLedMasks[7] = {
    0b000,
    0b001,
    0b010,
    0b100,
    0b011,
    0b110,
    0b111,
};

[[nodiscard]] constexpr unsigned char layer_led_mask(unsigned char layer) {
  return layer <= 6 ? kLayerLedMasks[layer] : 0b000;
}

[[nodiscard]] constexpr int layer_led_y_offset(
    unsigned int top_to_bottom_index) {
  return 29 + static_cast<int>(top_to_bottom_index) * 10;
}

}  // namespace codex
