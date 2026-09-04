#pragma once

#include "codex/device_state.h"
#include "codex/lighting_compositor.h"

namespace codex {

enum class UiPointerPhase : unsigned char { Down, Move, Up, Cancel };
struct UiPointerEvent {
  UiPointerPhase phase;
  int x;
  int y;
};
using UiPointerCallback = void (*)(UiPointerEvent event, void* context);
enum class UiAction : unsigned char {
  CloseOverlay,
  SelectAuto,
  SelectUsb,
  SelectBle,
  SelectMixed,
  SelectBle1,
  SelectBle2,
  SelectBle3,
  ToggleBluetooth,
  ClearBonds,
  SetVolume,
  SetBrightness,
  SetJoystickSensitivity,
  TriggerArcade,
  CycleScreensaver,
  ToggleSmartScreensaver,
  CycleSuperStandby,
  ToggleAntiAccidentalShutdown,
  CyclePowerButtonMode,
  CycleAutoUltraStandby,
  ToggleUltraTouchWake,
  EnterProtectedStandby,
  UnlockProtected,
  AcknowledgeBatteryWarning,
};
using UiActionCallback = void (*)(UiAction action, unsigned char value,
                                  void* context);

void ui_init(const DeviceState& initial_state);
void ui_deinit();
void ui_release_resources();
void ui_apply_snapshot(const DeviceState& state,
                       const CompositedLighting& lighting);
void ui_set_pointer_callback(UiPointerCallback callback, void* context);
void ui_set_action_callback(UiActionCallback callback, void* context);
void ui_begin_arcade_entry();
void ui_enter_arcade();
void ui_begin_arcade_exit();
void ui_finish_arcade_exit();
void ui_set_arcade_control_mode(unsigned char mode);
[[nodiscard]] bool ui_arcade_active();

}  // namespace codex
