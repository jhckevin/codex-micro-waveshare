#include "recovery_guard.h"

#include "driver/gpio.h"

namespace {

#ifndef CONFIG_CODEX_RECOVERY_PATH_PROVEN
#define CONFIG_CODEX_RECOVERY_PATH_PROVEN 0
#endif

// This build-time switch is deliberately outside every runtime configuration
// surface. The native USB identity cannot strand the board unless the separate
// CH343/UART0 rescue path was explicitly proven first.
constexpr bool kRecoveryProven = CONFIG_CODEX_RECOVERY_PATH_PROVEN;

RecoveryInputs read_inputs() {
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << GPIO_NUM_0;
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  return {
      .recovery_proven = kRecoveryProven,
      .gpio0_low = gpio_get_level(GPIO_NUM_0) == 0,
  };
}

}  // namespace

RecoveryBootMode recovery_guard_detect() {
  return RecoveryGuard(read_inputs()).mode();
}

bool recovery_guard_allows_codex_usb() {
  return RecoveryGuard(read_inputs()).allows_codex_usb();
}
