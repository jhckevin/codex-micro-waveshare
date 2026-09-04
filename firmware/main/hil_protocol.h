#pragma once

#include "codex/device_event.h"

namespace codex {

enum class HilLineState : unsigned char { Collecting, Complete, Overflow };

class HilLineAccumulator {
 public:
  static constexpr unsigned int kCapacity = 192;

  [[nodiscard]] HilLineState push(char value) {
    if (value == '\r') return HilLineState::Collecting;
    if (value == '\n') {
      buffer_[length_] = '\0';
      return length_ == 0U ? HilLineState::Collecting
                           : HilLineState::Complete;
    }
    if (length_ + 1U >= kCapacity) {
      reset();
      return HilLineState::Overflow;
    }
    buffer_[length_++] = value;
    buffer_[length_] = '\0';
    return HilLineState::Collecting;
  }

  [[nodiscard]] const char* line() const { return buffer_; }
  [[nodiscard]] unsigned int length() const { return length_; }

  void reset() {
    length_ = 0;
    buffer_[0] = '\0';
  }

 private:
  char buffer_[kCapacity]{};
  unsigned int length_{};
};

enum class HilCommandType : unsigned char {
  None,
  Ping,
  Snapshot,
  Metrics,
  Trace,
  Touch,
  Tap,
  Key,
  PowerButton,
  UltraStandby,
  UltraWake,
  Joystick,
  Encoder,
  ClearBleBonds,
  LightingStress,
  ScreenCrc,
  ScreenRead,
  AppConnect,
  AppHeartbeat,
  AppDisconnect,
  SetTransport,
  SetLayer,
  SetRoutingLayerCount,
  SetLayer1Route,
  SetHigherCodexMask,
  AppKeyLighting,
  RoutingSnapshot,
};

enum class HilTouchPhase : unsigned char { Down, Move, Up, Cancel };

struct HilTouchCommand {
  HilTouchPhase phase{HilTouchPhase::Down};
  unsigned char track_id{};
  int x{};
  int y{};
};

struct HilScreenRegion {
  unsigned short x{};
  unsigned short y{};
  unsigned short width{};
  unsigned short height{};
};

struct HilCommand {
  bool claimed{};
  bool valid{};
  HilCommandType type{HilCommandType::None};
  HilTouchCommand touch{};
  HilScreenRegion region{};
  ControlId control{ControlId::Agent0};
  bool key_down{};
  float angle{};
  float distance{};
  signed char encoder_step{};
  unsigned char ble_slot{};
  bool enabled{};
  unsigned char layer{};
  TransportMode transport{TransportMode::Auto};
  unsigned char layer_count{};
  unsigned char physical_index{};
  unsigned char target_layer{};
  ControlGroup group{ControlGroup::Agent};
  unsigned char mask{};
  unsigned char control_id{};
  Lighting lighting{};
  const char* error{};
};

[[nodiscard]] HilCommand parse_hil_command(const char* line);

}  // namespace codex
