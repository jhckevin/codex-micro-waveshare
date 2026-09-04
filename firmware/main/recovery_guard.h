#pragma once

enum class RecoveryBootMode : unsigned char {
  Normal,
  Maintenance,
};

struct RecoveryInputs {
  bool recovery_proven{false};
  bool gpio0_low{false};
};

class RecoveryGuard {
 public:
  explicit constexpr RecoveryGuard(RecoveryInputs inputs) : inputs_(inputs) {}

  [[nodiscard]] constexpr RecoveryBootMode mode() const {
    return inputs_.gpio0_low ? RecoveryBootMode::Maintenance
                             : RecoveryBootMode::Normal;
  }

  [[nodiscard]] constexpr bool allows_codex_usb() const {
    return inputs_.recovery_proven && mode() == RecoveryBootMode::Normal;
  }

 private:
  RecoveryInputs inputs_;
};

[[nodiscard]] RecoveryBootMode recovery_guard_detect();
[[nodiscard]] bool recovery_guard_allows_codex_usb();
