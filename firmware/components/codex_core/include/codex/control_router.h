#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace codex {

enum class ControlGroup : std::uint8_t {
  Agent = 0,
  Command,
  Encoder,
  Joystick,
};

enum class RouteDestination : std::uint8_t {
  Codex = 0,
  App,
};

inline constexpr std::size_t kControlGroupCount = 4;
inline constexpr std::uint8_t kPhysicalControlsPerGroup = 6;
inline constexpr std::uint8_t kMinimumLayerCount = 2;
inline constexpr std::uint8_t kMaximumLayerCount = 6;
inline constexpr std::uint8_t kLayer1CommandOverrideTarget = 7;
inline constexpr std::uint8_t kPrivateControlIdCount =
    (kMaximumLayerCount + 1U) * kPhysicalControlsPerGroup;

[[nodiscard]] constexpr std::size_t control_group_index(
    ControlGroup group) noexcept {
  return static_cast<std::size_t>(group);
}

struct ControlAddress {
  ControlGroup group{ControlGroup::Agent};
  std::uint8_t id{};
};

struct RoutedControl {
  bool valid{};
  RouteDestination destination{RouteDestination::Codex};
  ControlAddress address{};
};

struct RoutingConfig {
  bool app_session_active{};
  bool layer_routing_enabled{};
  std::uint8_t configured_layer_count{kMaximumLayerCount};

  // Zero preserves Codex. Target 7 routes a Layer 1 command key to the
  // shadow identifiers 36..41, outside every physical layer. Agent keys
  // cannot be overridden.
  std::array<std::uint8_t, kPhysicalControlsPerGroup>
      layer1_command_app_targets{};

  // Layers 2..6 x control group. Set bits route selected controls back to
  // their Layer 1 Codex equivalents.
  std::array<std::array<std::uint8_t, kControlGroupCount>,
             kMaximumLayerCount - 1>
      higher_codex_masks{};
};

[[nodiscard]] constexpr bool control_group_is_valid(
    ControlGroup group) noexcept {
  return control_group_index(group) < kControlGroupCount;
}

[[nodiscard]] constexpr RoutedControl route_control(
    const RoutingConfig& config, std::uint8_t layer, ControlGroup group,
    std::uint8_t physical_index) noexcept {
  if (layer < 1 || layer > kMaximumLayerCount ||
      physical_index >= kPhysicalControlsPerGroup ||
      !control_group_is_valid(group)) {
    return {};
  }

  const ControlAddress codex_address{group, physical_index};
  const auto codex_route = RoutedControl{
      true,
      RouteDestination::Codex,
      codex_address,
  };

  if (!config.app_session_active || !config.layer_routing_enabled) {
    return codex_route;
  }

  if (layer == 1) {
    if (group != ControlGroup::Command) {
      return codex_route;
    }

    const std::uint8_t target_layer =
        config.layer1_command_app_targets[physical_index];
    if (target_layer != kLayer1CommandOverrideTarget) {
      return codex_route;
    }

    return {
        true,
        RouteDestination::App,
        {group, static_cast<std::uint8_t>(
                    (target_layer - 1U) * kPhysicalControlsPerGroup +
                    physical_index)},
    };
  }

  const std::uint8_t passthrough_mask =
      config.higher_codex_masks[layer - 2U][control_group_index(group)];
  if ((passthrough_mask & (1U << physical_index)) != 0U) {
    return codex_route;
  }

  return {
      true,
      RouteDestination::App,
      {group, static_cast<std::uint8_t>(
                  (layer - 1U) * kPhysicalControlsPerGroup + physical_index)},
  };
}

[[nodiscard]] constexpr std::uint8_t normalized_layer_count(
    std::uint8_t configured_layer_count) noexcept {
  return configured_layer_count < kMinimumLayerCount ||
                 configured_layer_count > kMaximumLayerCount
             ? kMaximumLayerCount
             : configured_layer_count;
}

[[nodiscard]] constexpr std::uint8_t next_configured_layer(
    std::uint8_t current_layer, std::uint8_t configured_layer_count) noexcept {
  const std::uint8_t count = normalized_layer_count(configured_layer_count);
  if (current_layer < 1 || current_layer >= count) {
    return 1;
  }
  return static_cast<std::uint8_t>(current_layer + 1U);
}

}  // namespace codex
