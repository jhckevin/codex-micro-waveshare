#pragma once

namespace codex {

constexpr unsigned int kSettingsPageCount = 2;
constexpr unsigned int kAutoShutdownOptions[] = {
    3600, 7200, 10800, 18000, 0,
};

[[nodiscard]] constexpr unsigned int auto_shutdown_option(
    unsigned int index) {
  return index < sizeof(kAutoShutdownOptions) /
                     sizeof(kAutoShutdownOptions[0])
             ? kAutoShutdownOptions[index]
             : 7200;
}

}  // namespace codex
