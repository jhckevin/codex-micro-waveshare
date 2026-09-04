#include "codex/ui.h"

#include "codex/ambient_strip.h"
#include "codex/control_layout.h"
#include "codex/ambient_frame_lut.h"
#include "codex/ambient_render_policy.h"
#include "codex/battery_display.h"
#include "codex/firmware_version.h"
#include "codex/icon_id.h"
#include "codex/keycap_catalog.h"
#include "codex/layer_led_pattern.h"
#include "codex/user_content_storage.h"
#include "codex/lighting_lut.h"
#include "codex/settings_paging.h"
#include "codex/visual_tuning.h"
#include "dial_images.h"
#include "glow_images.h"
#include "theme.h"
#include "keycap_icons.h"

#include "esp_heap_caps.h"

#include <math.h>
#include <string.h>


namespace codex {
namespace {

struct KeyVisual {
  lv_obj_t* glow{};
  lv_obj_t* cap{};
  lv_obj_t* inset{};
  lv_obj_t* label{};
  lv_obj_t* icon{};
  lv_image_dsc_t custom_icon{};
  unsigned char* custom_icon_pixels{};
  unsigned int custom_icon_hash{};
  int resting_y{};
  bool pressed{};
  bool light_ready{};
  bool light_on{};
  LightEffect light_effect{LightEffect::Off};
  unsigned int light_color{};
  unsigned char light_brightness{};
  lv_opa_t light_opacity{};
  unsigned char breath_level{0xFF};
};

struct ArcadeVisual {
  lv_obj_t* screen{};
  lv_obj_t* ring{};
  KeyVisual agent{};
  lv_obj_t* joystick_well{};
  lv_obj_t* joystick_stick{};
  lv_obj_t* dpad{};
  lv_obj_t* dpad_keys[4]{};
  lv_obj_t* touch_region{};
  lv_obj_t* left_mode_label{};
  lv_obj_t* right_mode_label{};
  unsigned char control_mode{};
  unsigned char dpad_direction{0xFF};
  bool ring_ready{};
  lv_color_t ring_color{};
  lv_color_t ring_border_color{};
  lv_opa_t ring_opacity{};
  bool session{};
  bool active{};
};

struct UiState {
  lv_obj_t* classic_screen{};
  lv_obj_t* ambient{};
  lv_obj_t* ambient_strips[kAmbientStripTileCount]{};
  // Each tile alternates between two immutable source frames. LVGL can keep
  // reading the front frame while the next one is composed, eliminating the
  // live PSRAM mutation that produced intermittent black corner tiles.
  lv_image_dsc_t ambient_strip_images[kAmbientStripTileCount][2]{};
  unsigned char* ambient_strip_buffers[kAmbientStripTileCount][2]{};
  unsigned char ambient_strip_front_bank[kAmbientStripTileCount]{};
  KeyVisual agents[kAgentCount]{};
  KeyVisual commands[kCommandCount]{};
  lv_obj_t* dial_shine{};
  lv_obj_t* joystick_stick{};
  lv_obj_t* layer_leds[3]{};
  lv_obj_t* input_surface{};
  UiPointerCallback pointer_callback{};
  void* pointer_context{};
  lv_obj_t* connection_overlay{};
  lv_obj_t* settings_page_objects[kSettingsPageCount]{};
  lv_obj_t* settings_page_buttons[2]{};
  lv_obj_t* settings_dots[kSettingsPageCount]{};
  lv_obj_t* connection_status{};
  lv_obj_t* connection_dot{};
  lv_obj_t* ble_indicator_dot{};
  lv_obj_t* mode_buttons[4]{};
  lv_obj_t* slot_buttons[3]{};
  lv_obj_t* bluetooth_button{};
  lv_obj_t* bluetooth_value{};
  lv_obj_t* volume_slider{};
  lv_obj_t* volume_value{};
  lv_obj_t* brightness_slider{};
  lv_obj_t* brightness_value{};
  lv_obj_t* joystick_sensitivity_slider{};
  lv_obj_t* joystick_sensitivity_value{};
  lv_obj_t* battery_status{};
  lv_obj_t* battery_details{};
  lv_obj_t* super_standby_button{};
  lv_obj_t* super_standby_value{};
  lv_obj_t* screensaver_button{};
  lv_obj_t* screensaver_value{};
  lv_obj_t* smart_screensaver_button{};
  lv_obj_t* smart_screensaver_value{};
  lv_obj_t* anti_shutdown_button{};
  lv_obj_t* anti_shutdown_value{};
  lv_obj_t* power_button_mode_value{};
  lv_obj_t* auto_ultra_value{};
  lv_obj_t* ultra_touch_wake_value{};
  lv_obj_t* protected_overlay{};
  lv_obj_t* protected_slider{};
  lv_obj_t* battery_warning_overlay{};
  lv_obj_t* battery_warning_text{};
  bool updating_settings{};
  UiActionCallback action_callback{};
  void* action_context{};
  DeviceState last_state{};
  CompositedLighting last_lighting{};
  bool has_snapshot{};
  bool ambient_ready{};
  lv_color_t ambient_color{};
  lv_opa_t ambient_bg_opacity{};
  lv_color_t ambient_strip_colors[kAmbientStripTileCount]{};
  lv_opa_t ambient_strip_opacities[kAmbientStripTileCount]{};
  unsigned int ambient_strip_visible_mask{};
  unsigned int ambient_strip_phase{};
  LightEffect ambient_strip_effect{LightEffect::Off};
  bool ambient_strip_phase_ready{};
  bool ambient_strips_ready{};
  unsigned int settings_page{};
  bool ready{};
  ArcadeVisual arcade{};
} ui;

void overlay_action(lv_event_t* event) {
  if (ui.action_callback == nullptr || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const UiAction action = static_cast<UiAction>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  ui.action_callback(action, 0, ui.action_context);
}

void ble_slot_long_action(lv_event_t* event) {
  if (ui.action_callback == nullptr ||
      lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) return;
  const unsigned char slot = static_cast<unsigned char>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (slot < 1 || slot > 3) return;
  ui.action_callback(UiAction::ClearBonds, slot, ui.action_context);
}

void slider_action(lv_event_t* event) {
  if (ui.action_callback == nullptr || ui.updating_settings ||
      lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  const UiAction action = static_cast<UiAction>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  const int value = lv_slider_get_value(
      static_cast<lv_obj_t*>(lv_event_get_target(event)));
  ui.action_callback(action, static_cast<unsigned char>(value * 10),
                     ui.action_context);
}

void protected_slider_action(lv_event_t* event) {
  if (ui.action_callback == nullptr ||
      lv_event_get_code(event) != LV_EVENT_RELEASED) return;
  const int value = lv_slider_get_value(ui.protected_slider);
  if (value >= 98) {
    ui.action_callback(UiAction::UnlockProtected, 100, ui.action_context);
  } else {
    lv_slider_set_value(ui.protected_slider, 0, LV_ANIM_OFF);
  }
}

void set_settings_page(unsigned int page) {
  ui.settings_page =
      page < kSettingsPageCount ? page : kSettingsPageCount - 1U;
  for (unsigned int index = 0; index < kSettingsPageCount; ++index) {
    if (index == ui.settings_page) {
      lv_obj_remove_flag(ui.settings_page_objects[index], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ui.settings_page_objects[index], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ui.settings_page == 0) {
    lv_obj_add_state(ui.settings_page_buttons[0], LV_STATE_DISABLED);
  } else {
    lv_obj_remove_state(ui.settings_page_buttons[0], LV_STATE_DISABLED);
  }
  if (ui.settings_page + 1U == kSettingsPageCount) {
    lv_obj_add_state(ui.settings_page_buttons[1], LV_STATE_DISABLED);
  } else {
    lv_obj_remove_state(ui.settings_page_buttons[1], LV_STATE_DISABLED);
  }
  for (unsigned int index = 0; index < kSettingsPageCount; ++index) {
    lv_obj_set_style_bg_opa(ui.settings_dots[index],
                            static_cast<unsigned int>(index) == ui.settings_page
                                ? LV_OPA_COVER
                                : LV_OPA_30,
                            LV_PART_MAIN);
  }
}

void settings_page_action(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const bool next =
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)) != 0U;
  set_settings_page(
      settings_page_after_click(ui.settings_page, next));
}

lv_obj_t* make_overlay_button(lv_obj_t* parent, const char* text, int x, int y,
                              int width, UiAction action) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, width, 48);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_style_radius(button, 16, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xEDF1EF), LV_PART_MAIN);
  // Settings pages move as a whole. Software shadows on every child force
  // large invalidated areas during a swipe, so use a crisp inset border here.
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xD4DDD8),
                                LV_PART_MAIN);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  theme::make_label(label, theme::kInk, &lv_font_montserrat_14);
  lv_obj_center(label);
  lv_obj_add_event_cb(button, overlay_action, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
  return button;
}

lv_obj_t* make_settings_page_button(lv_obj_t* parent, const char* text, int x,
                                    bool next) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, 112, 32);
  lv_obj_set_pos(button, x, 369);
  lv_obj_set_style_radius(button, 13, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xE6ECE9), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xF1F4F2),
                            static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_PRESSED));
  lv_obj_set_style_bg_opa(button, LV_OPA_30,
                          static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                              static_cast<lv_style_selector_t>(
                                  LV_STATE_DISABLED));
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xD0D9D4),
                                LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  theme::make_label(label, theme::kInk, &lv_font_montserrat_12);
  lv_obj_center(label);
  lv_obj_add_event_cb(button, settings_page_action, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(next ? 1U : 0U));
  return button;
}

void make_connection_overlay(lv_obj_t* screen) {
  ui.connection_overlay = lv_obj_create(screen);
  lv_obj_set_size(ui.connection_overlay, 416, 424);
  lv_obj_align(ui.connection_overlay, LV_ALIGN_CENTER, 0, 0);
  theme::make_panel(ui.connection_overlay, 30);
  lv_obj_set_style_bg_color(ui.connection_overlay, lv_color_hex(0xF5F8F6), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ui.connection_overlay, 242, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(ui.connection_overlay, 36, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(ui.connection_overlay, LV_OPA_50, LV_PART_MAIN);
  lv_obj_remove_flag(ui.connection_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(ui.connection_overlay);
  lv_label_set_text(title, "Settings Center");
  theme::make_label(title, theme::kInk, &lv_font_montserrat_20);
  lv_obj_set_pos(title, 24, 20);
  make_overlay_button(ui.connection_overlay, "x", 350, 12, 44,
                      UiAction::CloseOverlay);

  lv_obj_t* page_one = lv_obj_create(ui.connection_overlay);
  lv_obj_set_size(page_one, 380, 286);
  lv_obj_set_pos(page_one, 18, 68);
  lv_obj_set_style_bg_opa(page_one, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(page_one, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page_one, 0, LV_PART_MAIN);
  lv_obj_remove_flag(page_one, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* page_two = lv_obj_create(ui.connection_overlay);
  lv_obj_set_size(page_two, 380, 286);
  lv_obj_set_pos(page_two, 18, 68);
  lv_obj_set_style_bg_opa(page_two, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(page_two, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page_two, 0, LV_PART_MAIN);
  lv_obj_remove_flag(page_two, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* page_three = lv_obj_create(ui.connection_overlay);
  lv_obj_set_size(page_three, 380, 286);
  lv_obj_set_pos(page_three, 18, 68);
  lv_obj_set_style_bg_opa(page_three, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(page_three, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(page_three, 0, LV_PART_MAIN);
  lv_obj_remove_flag(page_three, LV_OBJ_FLAG_SCROLLABLE);
  ui.settings_page_objects[0] = page_one;
  ui.settings_page_objects[1] = page_two;
  ui.settings_page_objects[2] = page_three;

  ui.mode_buttons[0] = make_overlay_button(page_one, "AUTO", 5, 0, 88,
                                           UiAction::SelectAuto);
  ui.mode_buttons[1] = make_overlay_button(page_one, "USB", 99, 0, 88,
                                           UiAction::SelectUsb);
  ui.mode_buttons[2] = make_overlay_button(page_one, "BLE", 193, 0, 88,
                                           UiAction::SelectBle);
  ui.mode_buttons[3] = make_overlay_button(page_one, "MIX", 287, 0, 88,
                                           UiAction::SelectMixed);
  ui.slot_buttons[0] = make_overlay_button(page_one, "BLE 1", 5, 54, 118,
                                           UiAction::SelectBle1);
  ui.slot_buttons[1] = make_overlay_button(page_one, "BLE 2", 131, 54, 118,
                                           UiAction::SelectBle2);
  ui.slot_buttons[2] = make_overlay_button(page_one, "BLE 3", 257, 54, 118,
                                           UiAction::SelectBle3);
  for (unsigned int index = 0; index < 3; ++index) {
    lv_obj_add_event_cb(ui.slot_buttons[index], ble_slot_long_action,
                        LV_EVENT_LONG_PRESSED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(index + 1U)));
  }
  ui.ble_indicator_dot = lv_obj_create(page_one);
  lv_obj_set_size(ui.ble_indicator_dot, 9, 9);
  lv_obj_set_pos(ui.ble_indicator_dot, 363, 59);
  lv_obj_set_style_radius(ui.ble_indicator_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.ble_indicator_dot, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.ble_indicator_dot, lv_color_hex(0xD8E5E0), LV_PART_MAIN);
  ui.connection_dot = lv_obj_create(page_one);
  lv_obj_set_size(ui.connection_dot, 10, 10);
  lv_obj_set_pos(ui.connection_dot, 7, 118);
  lv_obj_set_style_radius(ui.connection_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.connection_dot, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.connection_dot, lv_color_hex(0x98A39D),
                            LV_PART_MAIN);
  ui.connection_status = lv_label_create(page_one);
  theme::make_label(ui.connection_status, 0x52615A, &lv_font_montserrat_14);
  lv_obj_set_pos(ui.connection_status, 25, 113);

  lv_obj_t* battery_card = lv_obj_create(page_one);
  lv_obj_set_size(battery_card, 185, 42);
  lv_obj_set_pos(battery_card, 190, 103);
  lv_obj_set_style_radius(battery_card, 13, LV_PART_MAIN);
  lv_obj_set_style_bg_color(battery_card, lv_color_hex(0xE8F2EE),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(battery_card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(battery_card, lv_color_hex(0xC8DAD2),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(battery_card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(battery_card, 0, LV_PART_MAIN);
  lv_obj_remove_flag(battery_card, LV_OBJ_FLAG_SCROLLABLE);
  ui.battery_status = lv_label_create(battery_card);
  theme::make_label(ui.battery_status, 0x24312B, &lv_font_montserrat_12);
  lv_obj_set_pos(ui.battery_status, 8, 4);
  ui.battery_details = lv_label_create(battery_card);
  theme::make_label(ui.battery_details, 0x66706B, &lv_font_montserrat_12);
  lv_obj_set_pos(ui.battery_details, 8, 22);

  lv_obj_t* volume_label = lv_label_create(page_one);
  lv_label_set_text(volume_label, "Key volume");
  theme::make_label(volume_label, theme::kInk, &lv_font_montserrat_14);
  lv_obj_set_pos(volume_label, 5, 148);
  ui.volume_value = lv_label_create(page_one);
  theme::make_label(ui.volume_value, 0x52615A, &lv_font_montserrat_14);
  lv_obj_set_pos(ui.volume_value, 337, 148);
  ui.volume_slider = lv_slider_create(page_one);
  lv_obj_set_size(ui.volume_slider, 370, 18);
  lv_obj_set_pos(ui.volume_slider, 5, 177);
  lv_slider_set_range(ui.volume_slider, 1, 10);
  lv_obj_set_style_bg_color(ui.volume_slider, lv_color_hex(0xD9E2DD),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.volume_slider, lv_color_hex(0x304FFE),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ui.volume_slider, lv_color_hex(0xFFFFFF),
                            LV_PART_KNOB);
  lv_obj_add_event_cb(
      ui.volume_slider, slider_action, LV_EVENT_VALUE_CHANGED,
      reinterpret_cast<void*>(static_cast<uintptr_t>(UiAction::SetVolume)));

  ui.bluetooth_button =
      make_overlay_button(page_one, "Bluetooth on", 5, 224, 178,
                          UiAction::ToggleBluetooth);
  ui.bluetooth_value = lv_obj_get_child(ui.bluetooth_button, 0);
  make_overlay_button(page_one, "Reset Bluetooth", 197, 224, 178,
                      UiAction::ClearBonds);

  lv_obj_t* brightness_label = lv_label_create(page_two);
  lv_label_set_text(brightness_label, "Screen brightness");
  theme::make_label(brightness_label, theme::kInk, &lv_font_montserrat_14);
  lv_obj_set_pos(brightness_label, 5, 0);
  ui.brightness_value = lv_label_create(page_two);
  theme::make_label(ui.brightness_value, 0x52615A, &lv_font_montserrat_14);
  lv_obj_set_pos(ui.brightness_value, 337, 0);
  ui.brightness_slider = lv_slider_create(page_two);
  lv_obj_set_size(ui.brightness_slider, 370, 14);
  lv_obj_set_pos(ui.brightness_slider, 5, 22);
  lv_slider_set_range(ui.brightness_slider, 1, 10);
  lv_obj_set_style_bg_color(ui.brightness_slider, lv_color_hex(0xD9E2DD),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.brightness_slider, lv_color_hex(0x2DA66F),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ui.brightness_slider, lv_color_hex(0xFFFFFF),
                            LV_PART_KNOB);
  lv_obj_add_event_cb(
      ui.brightness_slider, slider_action, LV_EVENT_VALUE_CHANGED,
      reinterpret_cast<void*>(static_cast<uintptr_t>(UiAction::SetBrightness)));

  lv_obj_t* sensitivity_label = lv_label_create(page_two);
  lv_label_set_text(sensitivity_label, "Joystick sensitivity");
  theme::make_label(sensitivity_label, theme::kInk, &lv_font_montserrat_14);
  lv_obj_set_pos(sensitivity_label, 5, 43);
  ui.joystick_sensitivity_value = lv_label_create(page_two);
  theme::make_label(ui.joystick_sensitivity_value, 0x52615A,
                    &lv_font_montserrat_14);
  lv_obj_set_pos(ui.joystick_sensitivity_value, 330, 43);
  ui.joystick_sensitivity_slider = lv_slider_create(page_two);
  lv_obj_set_size(ui.joystick_sensitivity_slider, 370, 14);
  lv_obj_set_pos(ui.joystick_sensitivity_slider, 5, 66);
  lv_slider_set_range(ui.joystick_sensitivity_slider, 5, 20);
  lv_obj_set_style_bg_color(ui.joystick_sensitivity_slider,
                            lv_color_hex(0xD9E2DD), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.joystick_sensitivity_slider,
                            lv_color_hex(0x304FFE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ui.joystick_sensitivity_slider,
                            lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_add_event_cb(
      ui.joystick_sensitivity_slider, slider_action, LV_EVENT_VALUE_CHANGED,
      reinterpret_cast<void*>(
          static_cast<uintptr_t>(UiAction::SetJoystickSensitivity)));

  ui.screensaver_button =
      make_overlay_button(page_two, "Screen 3m", 5, 91, 178,
                          UiAction::CycleScreensaver);
  lv_obj_set_height(ui.screensaver_button, 40);
  ui.screensaver_value = lv_obj_get_child(ui.screensaver_button, 0);
  ui.super_standby_button =
      make_overlay_button(page_two, "Off 2h", 197, 91, 178,
                          UiAction::CycleSuperStandby);
  lv_obj_set_height(ui.super_standby_button, 40);
  ui.super_standby_value =
      lv_obj_get_child(ui.super_standby_button, 0);
  ui.smart_screensaver_button =
      make_overlay_button(page_two, "Smart screen: On", 5, 139, 178,
                          UiAction::ToggleSmartScreensaver);
  lv_obj_set_height(ui.smart_screensaver_button, 40);
  ui.smart_screensaver_value = lv_obj_get_child(ui.smart_screensaver_button, 0);
  ui.anti_shutdown_button =
      make_overlay_button(page_two, "Anti-touch off", 197, 139, 178,
                          UiAction::ToggleAntiAccidentalShutdown);
  lv_obj_set_height(ui.anti_shutdown_button, 40);
  ui.anti_shutdown_value = lv_obj_get_child(ui.anti_shutdown_button, 0);
  lv_obj_t* protected_button =
      make_overlay_button(page_two, "Protected standby", 5, 187, 178,
                          UiAction::EnterProtectedStandby);
  lv_obj_set_height(protected_button, 40);
  lv_obj_t* arcade_button =
      make_overlay_button(page_two, "Arcade trigger", 197, 187, 178,
                          UiAction::TriggerArcade);
  lv_obj_set_height(arcade_button, 40);
  lv_obj_t* firmware_version = lv_label_create(page_two);
  lv_label_set_text(firmware_version, kFirmwareSettingsLabel);
  theme::make_label(firmware_version, 0x66706B, &lv_font_montserrat_12);
  lv_obj_align(firmware_version, LV_ALIGN_TOP_RIGHT, -5, 249);

  lv_obj_t* power_mode_button =
      make_overlay_button(page_three, "PWR: Connected standby", 5, 0, 370,
                          UiAction::CyclePowerButtonMode);
  lv_obj_set_height(power_mode_button, 48);
  ui.power_button_mode_value = lv_obj_get_child(power_mode_button, 0);
  lv_obj_t* auto_ultra_button =
      make_overlay_button(page_three, "Auto ultra: Off", 5, 58, 370,
                          UiAction::CycleAutoUltraStandby);
  lv_obj_set_height(auto_ultra_button, 48);
  ui.auto_ultra_value = lv_obj_get_child(auto_ultra_button, 0);
  lv_obj_t* touch_wake_button =
      make_overlay_button(page_three, "Ultra touch wake: Off", 5, 116, 370,
                          UiAction::ToggleUltraTouchWake);
  lv_obj_set_height(touch_wake_button, 48);
  ui.ultra_touch_wake_value = lv_obj_get_child(touch_wake_button, 0);
  lv_obj_t* ultra_note = lv_label_create(page_three);
  lv_label_set_text(
      ultra_note,
      "Ultra disconnects cleanly, sleeps deeply,\nand reconnects after wake.");
  theme::make_label(ultra_note, 0x66706B, &lv_font_montserrat_12);
  lv_obj_set_pos(ultra_note, 8, 180);

  ui.settings_page_buttons[0] =
      make_settings_page_button(ui.connection_overlay, "< Previous", 64,
                                false);
  ui.settings_page_buttons[1] =
      make_settings_page_button(ui.connection_overlay, "Next >", 240, true);

  for (unsigned int index = 0; index < kSettingsPageCount; ++index) {
    ui.settings_dots[index] = lv_obj_create(ui.connection_overlay);
    lv_obj_set_size(ui.settings_dots[index], 7, 7);
    lv_obj_set_pos(ui.settings_dots[index], 190 + index * 14, 407);
    lv_obj_set_style_radius(ui.settings_dots[index], LV_RADIUS_CIRCLE,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(ui.settings_dots[index], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.settings_dots[index],
                              lv_color_hex(0x304FFE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui.settings_dots[index],
                            index == 0 ? LV_OPA_COVER : LV_OPA_30,
                            LV_PART_MAIN);
  }
  set_settings_page(0);
  lv_obj_add_flag(ui.connection_overlay, LV_OBJ_FLAG_HIDDEN);

  ui.protected_overlay = lv_obj_create(screen);
  lv_obj_set_size(ui.protected_overlay, 480, 480);
  lv_obj_set_pos(ui.protected_overlay, 0, 0);
  lv_obj_set_style_bg_color(ui.protected_overlay, lv_color_hex(0xF4F7F6),
                            LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.protected_overlay, 0, LV_PART_MAIN);
  lv_obj_remove_flag(ui.protected_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* logo = lv_image_create(ui.protected_overlay);
  lv_image_set_src(logo, icon_for_keycap("CODEX"));
  lv_obj_set_style_image_recolor(logo, lv_color_hex(0x162A68), LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -80);
  ui.protected_slider = lv_slider_create(ui.protected_overlay);
  lv_obj_set_size(ui.protected_slider, 380, 70);
  lv_obj_align(ui.protected_slider, LV_ALIGN_CENTER, 0, 20);
  lv_slider_set_range(ui.protected_slider, 0, 100);
  lv_obj_set_style_radius(ui.protected_slider, 22, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.protected_slider, lv_color_hex(0xDDE5E2),
                            LV_PART_MAIN);
  lv_obj_set_style_radius(ui.protected_slider, 22, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ui.protected_slider, lv_color_hex(0x304FFE),
                            LV_PART_INDICATOR);
  lv_obj_set_style_width(ui.protected_slider, 66, LV_PART_KNOB);
  lv_obj_set_style_height(ui.protected_slider, 58, LV_PART_KNOB);
  lv_obj_set_style_radius(ui.protected_slider, 18, LV_PART_KNOB);
  lv_obj_set_ext_click_area(ui.protected_slider, 12);
  lv_obj_add_event_cb(ui.protected_slider, protected_slider_action,
                      LV_EVENT_RELEASED, nullptr);
  lv_obj_t* unlock_text = lv_label_create(ui.protected_overlay);
  lv_label_set_text(unlock_text, "Slide to exit protected standby");
  theme::make_label(unlock_text, 0x52615A, &lv_font_montserrat_14);
  lv_obj_align(unlock_text, LV_ALIGN_CENTER, 0, 84);
  lv_obj_add_flag(ui.protected_overlay, LV_OBJ_FLAG_HIDDEN);

  ui.battery_warning_overlay = lv_obj_create(screen);
  lv_obj_set_size(ui.battery_warning_overlay, 360, 220);
  lv_obj_align(ui.battery_warning_overlay, LV_ALIGN_CENTER, 0, 0);
  theme::make_panel(ui.battery_warning_overlay, 28);
  lv_obj_set_style_bg_color(ui.battery_warning_overlay,
                            lv_color_hex(0xFFF8F0), LV_PART_MAIN);
  ui.battery_warning_text = lv_label_create(ui.battery_warning_overlay);
  lv_label_set_text(ui.battery_warning_text, "Battery warning");
  theme::make_label(ui.battery_warning_text, 0x7A351A,
                    &lv_font_montserrat_20);
  lv_obj_align(ui.battery_warning_text, LV_ALIGN_TOP_MID, 0, 34);
  make_overlay_button(ui.battery_warning_overlay, "Got it", 80, 126, 168,
                      UiAction::AcknowledgeBatteryWarning);
  lv_obj_add_flag(ui.battery_warning_overlay, LV_OBJ_FLAG_HIDDEN);
}

lv_opa_t brightness_opa(const Lighting& lighting, float phase) {
  if (lighting.effect == LightEffect::Off || lighting.brightness <= 0.0F) {
    return static_cast<lv_opa_t>(LV_OPA_TRANSP);
  }
  float value = lighting.brightness * effect_envelope(lighting.effect, phase);
  if (value < 0.0F) value = 0.0F;
  if (value > 1.0F) value = 1.0F;
  return static_cast<lv_opa_t>(20 + value * 190.0F);
}

void set_animated_y(void* target, int32_t value) {
  lv_obj_set_y(static_cast<lv_obj_t*>(target), value);
}

void animate_y(lv_obj_t* object, int from, int to, unsigned int duration) {
  lv_anim_delete(object, set_animated_y);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, object);
  lv_anim_set_values(&animation, from, to);
  lv_anim_set_duration(&animation, duration);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, set_animated_y);
  lv_anim_start(&animation);
}

lv_obj_t* make_screw(lv_obj_t* parent, int x, int y) {
  lv_obj_t* screw = lv_obj_create(parent);
  lv_obj_set_size(screw, 18, 18);
  lv_obj_set_pos(screw, x, y);
  lv_obj_set_style_radius(screw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(screw, lv_color_hex(0x171918), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(screw, lv_color_hex(0x050505), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(screw, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_color(screw, lv_color_hex(0x343936), LV_PART_MAIN);
  lv_obj_set_style_border_width(screw, 2, LV_PART_MAIN);
  lv_obj_remove_flag(screw, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* mark = lv_label_create(screw);
  lv_label_set_text(mark, "+");
  theme::make_label(mark, 0x050505, &lv_font_montserrat_12);
  lv_obj_set_style_transform_rotation(mark, 450, LV_PART_MAIN);
  lv_obj_center(mark);
  return screw;
}

void make_case_typography(lv_obj_t* screen) {
  lv_obj_t* arrow = lv_label_create(screen);
  lv_label_set_text(arrow, LV_SYMBOL_UP);
  theme::make_label(arrow, theme::kInk, &lv_font_montserrat_24);
  lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, 23);

  lv_obj_t* left = lv_label_create(screen);
  lv_label_set_text(left, "Work Louder | OpenAI 2026");
  theme::make_label(left, 0x6B746F, &lv_font_montserrat_12);
  lv_obj_set_style_transform_rotation(left, 2700, LV_PART_MAIN);
  lv_obj_set_pos(left, 23, 323);

  lv_obj_t* right = lv_label_create(screen);
  lv_label_set_text(right, "You can just build things");
  theme::make_label(right, 0x626B66, &lv_font_montserrat_12);
  lv_obj_set_style_transform_rotation(right, 900, LV_PART_MAIN);
  lv_obj_set_pos(right, 451, 151);
}

const lv_image_dsc_t* builtin_icon_for_hash(unsigned int hash) {
  for (unsigned int index = 0; index < keycap_catalog_size(); ++index) {
    const char* id = keycap_catalog_id(index);
    if (icon_id_hash(id) == hash) return icon_for_keycap(id);
  }
  return nullptr;
}

bool load_custom_icon(KeyVisual& visual, unsigned int hash, bool force_reload) {
  if (visual.custom_icon_hash == hash &&
      visual.custom_icon_pixels != nullptr && !force_reload) {
    return true;
  }
  if (visual.custom_icon_pixels == nullptr) {
    visual.custom_icon_pixels = static_cast<unsigned char*>(
        heap_caps_malloc(kUserContentIconBytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (visual.custom_icon_pixels == nullptr) return false;
    visual.custom_icon.header.magic = LV_IMAGE_HEADER_MAGIC;
    visual.custom_icon.header.cf = LV_COLOR_FORMAT_A8;
    visual.custom_icon.header.flags = 0;
    visual.custom_icon.header.w = kUserContentIconWidth;
    visual.custom_icon.header.h = kUserContentIconHeight;
    visual.custom_icon.header.stride = kUserContentIconWidth;
    visual.custom_icon.header.reserved_2 = 0;
    visual.custom_icon.data_size = kUserContentIconBytes;
    visual.custom_icon.data = visual.custom_icon_pixels;
    visual.custom_icon.reserved = nullptr;
  }
  if (!read_active_user_content_icon_by_hash(
          hash, visual.custom_icon_pixels, kUserContentIconBytes)) {
    return false;
  }
  visual.custom_icon_hash = hash;
  return true;
}

void show_key_content(KeyVisual& visual, unsigned int app_hash,
                      const lv_image_dsc_t* classical_icon,
                      const char* classical_label,
                      bool force_reload = false) {
  const bool app_override = app_hash != 0U;
  if (app_override && app_hash == icon_id_hash("__blank")) {
    lv_obj_add_flag(visual.icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(visual.label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const lv_image_dsc_t* icon =
      app_override ? builtin_icon_for_hash(app_hash) : classical_icon;
  if (app_override && icon == nullptr &&
      load_custom_icon(visual, app_hash, force_reload)) {
    icon = &visual.custom_icon;
  }
  if (icon != nullptr) {
    lv_image_set_src(visual.icon, icon);
    lv_obj_remove_flag(visual.icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(visual.label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(visual.label, classical_label);
  lv_obj_remove_flag(visual.label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(visual.icon, LV_OBJ_FLAG_HIDDEN);
}

KeyVisual make_key(lv_obj_t* screen, const Rect& bounds, const char* text,
                   bool agent, unsigned int tint) {
  KeyVisual visual{};
  visual.resting_y = bounds.y;
  visual.glow = lv_image_create(screen);
  lv_image_set_src(visual.glow,
                   bounds.width > 100 ? &key_glow_wide : &key_glow_small);
  lv_obj_set_pos(visual.glow, bounds.x - 18, bounds.y - 18);
  lv_obj_set_style_image_recolor(visual.glow, lv_color_hex(tint), LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(visual.glow, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_image_opa(visual.glow, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_remove_flag(visual.glow, LV_OBJ_FLAG_SCROLLABLE);

  visual.cap = lv_obj_create(screen);
  lv_obj_set_size(visual.cap, bounds.width, bounds.height - 5);
  lv_obj_set_pos(visual.cap, bounds.x, bounds.y);
  theme::make_keycap(visual.cap, 17);
  visual.inset = lv_obj_create(visual.cap);
  const int inset_size = bounds.width > 100 ? 64 : 62;
  lv_obj_set_size(visual.inset, bounds.width > 100 ? 132 : inset_size, inset_size);
  lv_obj_center(visual.inset);
  lv_obj_set_style_radius(visual.inset, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(visual.inset, lv_color_hex(0xF8FAF9), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(visual.inset, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_border_color(visual.inset, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(visual.inset, 1, LV_PART_MAIN);
  lv_obj_remove_flag(visual.inset, LV_OBJ_FLAG_SCROLLABLE);

  visual.label = lv_label_create(visual.inset);
  lv_label_set_text(visual.label, text);
  theme::make_label(visual.label, agent ? theme::kAgentMark : theme::kInk,
                    agent ? &lv_font_montserrat_24 : &lv_font_montserrat_14);
  lv_obj_center(visual.label);
  visual.icon = lv_image_create(visual.inset);
  lv_obj_set_style_image_recolor(visual.icon, lv_color_hex(theme::kInk), LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(visual.icon, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_center(visual.icon);
  lv_obj_add_flag(visual.icon, LV_OBJ_FLAG_HIDDEN);
  return visual;
}

void make_dial(lv_obj_t* screen, const Rect& bounds) {
  ui.dial_shine = lv_image_create(screen);
  lv_image_set_src(ui.dial_shine, &dial_frames[0]);
  lv_obj_set_pos(ui.dial_shine, bounds.x, bounds.y);
  lv_obj_remove_flag(ui.dial_shine, LV_OBJ_FLAG_SCROLLABLE);
}

void make_joystick(lv_obj_t* screen, const Rect& bounds) {
  lv_obj_t* well = lv_obj_create(screen);
  lv_obj_set_size(well, bounds.width, bounds.height - 5);
  lv_obj_set_pos(well, bounds.x, bounds.y);
  theme::make_keycap(well, 17);
  lv_obj_set_style_border_color(well, lv_color_hex(0x555A57), LV_PART_MAIN);
  lv_obj_set_style_border_width(well, 3, LV_PART_MAIN);
  ui.joystick_stick = lv_obj_create(well);
  lv_obj_set_size(ui.joystick_stick, 60, 60);
  lv_obj_center(ui.joystick_stick);
  lv_obj_set_style_radius(ui.joystick_stick, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.joystick_stick, lv_color_hex(0x202221), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(ui.joystick_stick, lv_color_hex(0x050505), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(ui.joystick_stick, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_color(ui.joystick_stick, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.joystick_stick, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(ui.joystick_stick, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(ui.joystick_stick, LV_OPA_40, LV_PART_MAIN);
  lv_obj_remove_flag(ui.joystick_stick, LV_OBJ_FLAG_SCROLLABLE);
}

#if 0  // Disabled opaque ring experiment; retained briefly for visual A/B.
void make_ambient_segments_opaque(lv_obj_t* screen) {
  constexpr short positions[UiState::kAmbientSegmentCount][4] = {
      {64, 9, kAmbientBarLength, kAmbientBarThickness},
      {159, 9, kAmbientBarLength, kAmbientBarThickness},
      {254, 9, kAmbientBarLength, kAmbientBarThickness},
      {349, 9, kAmbientBarLength, kAmbientBarThickness},
      {462, 64, kAmbientBarThickness, kAmbientBarLength},
      {462, 159, kAmbientBarThickness, kAmbientBarLength},
      {462, 254, kAmbientBarThickness, kAmbientBarLength},
      {462, 349, kAmbientBarThickness, kAmbientBarLength},
      {349, 462, kAmbientBarLength, kAmbientBarThickness},
      {254, 462, kAmbientBarLength, kAmbientBarThickness},
      {159, 462, kAmbientBarLength, kAmbientBarThickness},
      {64, 462, kAmbientBarLength, kAmbientBarThickness},
      {9, 349, kAmbientBarThickness, kAmbientBarLength},
      {9, 254, kAmbientBarThickness, kAmbientBarLength},
      {9, 159, kAmbientBarThickness, kAmbientBarLength},
      {9, 64, kAmbientBarThickness, kAmbientBarLength},
  };
  const auto make_layer = [screen, &positions](
                              lv_obj_t** output, int length_expansion,
                              int thickness_expansion) {
    for (unsigned int index = 0; index < UiState::kAmbientSegmentCount;
         ++index) {
      const bool horizontal = positions[index][2] > positions[index][3];
      const int width =
          positions[index][2] +
          (horizontal ? length_expansion : thickness_expansion);
      const int height =
          positions[index][3] +
          (horizontal ? thickness_expansion : length_expansion);
      lv_obj_t* layer = lv_obj_create(screen);
      output[index] = layer;
      lv_obj_set_size(layer, width, height);
      lv_obj_set_pos(layer,
                     positions[index][0] - (width - positions[index][2]) / 2,
                     positions[index][1] - (height - positions[index][3]) / 2);
      lv_obj_set_style_radius(layer, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(layer, lv_color_hex(0x07100D), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(layer, 0, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(layer, 0, LV_PART_MAIN);
      lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    }
  };
  // Draw broad-to-narrow. Every pixel is already composed over the ring
  // background, so these layers recover a soft halo without alpha sprites or
  // runtime shadow blur.
  make_layer(ui.ambient_halos, 12, 4);
  make_layer(ui.ambient_mids, 6, 2);
  make_layer(ui.ambient_segments, 0, 0);
  for (unsigned int layer_index = 0; layer_index < 3; ++layer_index) {
    for (unsigned int mover = 0; mover < kAmbientSnakeMoverCount; ++mover) {
      lv_obj_t* object = lv_obj_create(screen);
      ui.ambient_movers[mover][layer_index] = object;
      lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(object, lv_color_hex(0x07100D),
                                LV_PART_MAIN);
      lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
  }
}

unsigned int rainbow_color(unsigned int index) {
  constexpr unsigned int colors[] = {
      0xFF375F, 0xFF9F0A, 0xFFD60A, 0x32D74B, 0x64D2FF, 0x0A84FF, 0x5E5CE6, 0xBF5AF2};
  return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

void render_ambient_opaque(const CompositedLighting& lighting) {
  const Lighting& effect = lighting.ambient;
  const unsigned int phase_index =
      ambient_motion_phase_index(lighting.ambient_phase);
  const float envelope = effect_envelope(effect.effect, lighting.ambient_phase);
  const float master = effect.brightness * envelope;
  const unsigned int display_color = mix_ambient_white(effect.color);
  const unsigned int moving_color =
      ambient_segment_color(effect.effect, effect.color);
  constexpr unsigned int kScreenColor = 0x07100D;
  const unsigned int ambient_source_color =
      effect.effect == LightEffect::Off ? 0xE4ECE8 : display_color;
  const lv_opa_t ambient_bg_opacity =
      effect.effect == LightEffect::Off
          ? static_cast<lv_opa_t>(LV_OPA_60)
          : static_cast<lv_opa_t>(
                ambient_background_opacity(effect.brightness));
  const unsigned int ambient_base_color =
      compose_rgb(ambient_source_color, kScreenColor, ambient_bg_opacity);
  const lv_color_t ambient_color = lv_color_hex(ambient_base_color);
  if (!ui.ambient_ready || !lv_color_eq(ui.ambient_color, ambient_color)) {
    lv_obj_set_style_bg_color(ui.ambient, ambient_color, LV_PART_MAIN);
    ui.ambient_color = ambient_color;
  }
  if (!ui.ambient_ready || ui.ambient_bg_opacity != LV_OPA_COVER) {
    lv_obj_set_style_bg_opa(ui.ambient, LV_OPA_COVER, LV_PART_MAIN);
    ui.ambient_bg_opacity = LV_OPA_COVER;
  }
  const unsigned int head =
      (phase_index * UiState::kAmbientSegmentCount) /
      kAmbientMotionSampleCount;
  const bool snake = effect.effect == LightEffect::Snake &&
                     effect.brightness > 0.0F;
  if (!snake && ui.ambient_movers_visible) {
    for (auto& mover : ui.ambient_movers) {
      for (lv_obj_t* layer : mover) lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
    }
    ui.ambient_movers_visible = false;
  }
  for (unsigned int index = 0; index < UiState::kAmbientSegmentCount; ++index) {
    if (snake) {
      if (ui.ambient_layer_visible[index]) {
        lv_obj_add_flag(ui.ambient_halos[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.ambient_mids[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.ambient_segments[index], LV_OBJ_FLAG_HIDDEN);
        ui.ambient_layer_visible[index] = false;
      }
      continue;
    }
    float strength = master;
    unsigned int segment_color = moving_color;
    if (effect.effect == LightEffect::Rainbow) {
      segment_color =
          mix_ambient_motion_white(rainbow_color(index + head));
    } else if (effect.effect == LightEffect::Gradient) {
      const unsigned int mix = static_cast<unsigned int>((index * 180U) /
                                                          UiState::kAmbientSegmentCount);
      segment_color = compose_rgb(moving_color, 0xFFFFFF, mix);
    }
    if (effect.effect == LightEffect::Off) strength = 0.0F;
    strength = local_light_strength(strength);
    if (strength > 1.0F) strength = 1.0F;
    const auto strength_byte =
        static_cast<unsigned char>(strength * 255.0F);
    const bool visible = strength_byte != 0U;
    if (!ui.ambient_ready || ui.ambient_layer_visible[index] != visible) {
      lv_obj_t* layers[] = {ui.ambient_halos[index], ui.ambient_mids[index],
                            ui.ambient_segments[index]};
      for (lv_obj_t* layer : layers) {
        if (visible) {
          lv_obj_remove_flag(layer, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
        }
      }
      ui.ambient_layer_visible[index] = visible;
    }
    if (!visible) continue;
    const lv_color_t halo_color = lv_color_hex(ambient_layer_color(
        ambient_base_color, segment_color, strength_byte, 8U));
    const lv_color_t mid_color = lv_color_hex(ambient_layer_color(
        ambient_base_color, segment_color, strength_byte, 18U));
    const lv_color_t core_color = lv_color_hex(ambient_layer_color(
        ambient_base_color, segment_color, strength_byte, 30U));
    if (!ui.ambient_ready ||
        !lv_color_eq(ui.ambient_halo_colors[index], halo_color)) {
      lv_obj_set_style_bg_color(ui.ambient_halos[index], halo_color,
                                LV_PART_MAIN);
      ui.ambient_halo_colors[index] = halo_color;
    }
    if (!ui.ambient_ready ||
        !lv_color_eq(ui.ambient_mid_colors[index], mid_color)) {
      lv_obj_set_style_bg_color(ui.ambient_mids[index], mid_color,
                                LV_PART_MAIN);
      ui.ambient_mid_colors[index] = mid_color;
    }
    if (!ui.ambient_ready ||
        !lv_color_eq(ui.segment_colors[index], core_color)) {
      lv_obj_set_style_bg_color(ui.ambient_segments[index], core_color,
                                LV_PART_MAIN);
      ui.segment_colors[index] = core_color;
    }
  }
  if (snake) {
    constexpr unsigned char kTailStrengths[kAmbientSnakeMoverCount] = {
        255, 150, 65};
    constexpr short kLengths[3] = {60, 54, 46};
    constexpr short kThicknesses[3] = {11, 9, 7};
    constexpr unsigned int kLifts[3] = {10, 25, 45};
    for (unsigned int mover = 0; mover < kAmbientSnakeMoverCount; ++mover) {
      const unsigned int mover_phase = ambient_tail_phase(phase_index, mover);
      const AmbientPathPoint point = ambient_path_point(mover_phase);
      const unsigned char strength = static_cast<unsigned char>(
          effect.brightness * static_cast<float>(kTailStrengths[mover]));
      for (unsigned int layer_index = 0; layer_index < 3; ++layer_index) {
        lv_obj_t* object = ui.ambient_movers[mover][layer_index];
        const int width = point.horizontal ? kLengths[layer_index]
                                           : kThicknesses[layer_index];
        const int height = point.horizontal ? kThicknesses[layer_index]
                                            : kLengths[layer_index];
        if (!ui.ambient_movers_visible ||
            ui.ambient_mover_phases[mover] != mover_phase) {
          lv_obj_set_size(object, width, height);
          lv_obj_set_pos(object, point.x - width / 2, point.y - height / 2);
        }
        const lv_color_t color = lv_color_hex(ambient_layer_color(
            ambient_base_color, moving_color, strength, kLifts[layer_index]));
        if (!ui.ambient_movers_visible ||
            !lv_color_eq(ui.ambient_mover_colors[mover][layer_index], color)) {
          lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
          ui.ambient_mover_colors[mover][layer_index] = color;
        }
        if (!ui.ambient_movers_visible) {
          lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
      }
      ui.ambient_mover_phases[mover] = mover_phase;
    }
    ui.ambient_movers_visible = true;
  }
  const bool wrapped =
      ui.ambient_phase_ready &&
      ambient_phase_wrapped(ui.ambient_phase_index, phase_index);
  if (wrapped) {
    // A complete repaint at the animation seam repairs either RGB buffer if a
    // previous interrupted transfer ever left one stale, without adding work
    // to normal frames.
    for (unsigned int index = 0; index < UiState::kAmbientSegmentCount;
         ++index) {
      if (snake) {
        if (index >= kAmbientSnakeMoverCount) continue;
        for (lv_obj_t* layer : ui.ambient_movers[index]) {
          lv_obj_invalidate(layer);
        }
      } else {
        if (!ui.ambient_layer_visible[index]) continue;
        lv_obj_invalidate(ui.ambient_halos[index]);
        lv_obj_invalidate(ui.ambient_mids[index]);
        lv_obj_invalidate(ui.ambient_segments[index]);
      }
    }
  }
  ui.ambient_phase_index = phase_index;
  ui.ambient_phase_ready = true;
  ui.ambient_ready = true;
}
#endif

bool make_ambient_strips(lv_obj_t* screen) {
  for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
    const unsigned int bytes = ambient_strip_pixel_count(tile);
    for (unsigned int bank = 0; bank < 2U; ++bank) {
      ui.ambient_strip_buffers[tile][bank] =
          static_cast<unsigned char*>(heap_caps_calloc(
              bytes, sizeof(unsigned char),
              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (ui.ambient_strip_buffers[tile][bank] == nullptr) {
        for (unsigned int release_tile = 0;
             release_tile < kAmbientStripTileCount; ++release_tile) {
          for (unsigned int release_bank = 0; release_bank < 2U;
               ++release_bank) {
            heap_caps_free(
                ui.ambient_strip_buffers[release_tile][release_bank]);
            ui.ambient_strip_buffers[release_tile][release_bank] = nullptr;
          }
        }
        return false;
      }
    }
  }

  for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
    const AmbientStripRect rect = ambient_strip_rect(tile);
    for (unsigned int bank = 0; bank < 2U; ++bank) {
      lv_image_dsc_t& image = ui.ambient_strip_images[tile][bank];
      image.header.magic = LV_IMAGE_HEADER_MAGIC;
      image.header.cf = LV_COLOR_FORMAT_A8;
      image.header.flags = 0;
      image.header.w = static_cast<unsigned int>(rect.width);
      image.header.h = static_cast<unsigned int>(rect.height);
      image.header.stride = static_cast<unsigned int>(rect.width);
      image.header.reserved_2 = 0;
      image.data_size = ambient_strip_pixel_count(tile);
      image.data = ui.ambient_strip_buffers[tile][bank];
      image.reserved = nullptr;
    }

    lv_obj_t* strip = lv_image_create(screen);
    ui.ambient_strips[tile] = strip;
    ui.ambient_strip_front_bank[tile] = 0U;
    lv_image_set_src(strip, &ui.ambient_strip_images[tile][0]);
    lv_obj_set_pos(strip, rect.x, rect.y);
    lv_obj_set_style_image_recolor_opa(strip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_image_opa(strip, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
  }
  ui.ambient_strips_ready = true;
  return true;
}

unsigned char* begin_ambient_strip_frame(unsigned int tile) {
  const unsigned int back_bank =
      static_cast<unsigned int>(ui.ambient_strip_front_bank[tile] ^ 1U);
  return ui.ambient_strip_buffers[tile][back_bank];
}

void commit_ambient_strip_frame(unsigned int tile) {
  const unsigned char back_bank =
      static_cast<unsigned char>(ui.ambient_strip_front_bank[tile] ^ 1U);
  lv_image_dsc_t* const image = &ui.ambient_strip_images[tile][back_bank];

  // A descriptor returns to the back bank every other update. Drop any decoded
  // cache entry only after its pixels are complete, then publish the descriptor
  // once. The active source is never modified in place.
  lv_image_cache_drop(image);
  lv_image_set_src(ui.ambient_strips[tile], image);
  ui.ambient_strip_front_bank[tile] = back_bank;
}

void set_ambient_strip_visible_mask(unsigned int mask) {
  if (!ui.ambient_strips_ready) mask = 0U;
  ui.ambient_strip_visible_mask =
      mask & ambient_strip_object_visibility_mask();
}

void update_ambient_strip_style(unsigned int mask, lv_color_t color,
                                lv_opa_t opacity) {
  for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
    const unsigned int bit = 1U << tile;
    if ((mask & bit) == 0U) continue;
    if (!ui.ambient_ready ||
        !lv_color_eq(ui.ambient_strip_colors[tile], color)) {
      lv_obj_set_style_image_recolor(ui.ambient_strips[tile], color,
                                     LV_PART_MAIN);
      ui.ambient_strip_colors[tile] = color;
    }
    if (!ui.ambient_ready || ui.ambient_strip_opacities[tile] != opacity) {
      lv_obj_set_style_image_opa(ui.ambient_strips[tile], opacity,
                                 LV_PART_MAIN);
      ui.ambient_strip_opacities[tile] = opacity;
    }
  }
}

void render_ambient(const CompositedLighting& lighting) {
  const Lighting& effect = lighting.ambient;
  const AmbientRenderPolicy policy = ambient_render_policy(effect.effect);
  const unsigned int frame_phase =
      ambient_frame_phase_index(lighting.ambient_phase);
  const unsigned char global_level =
      ambient_frame_global(effect.effect, frame_phase, effect.brightness);
  const unsigned int ambient_source_color =
      policy.colored_base ? ambient_diffuser_color(effect.color) : 0xE4ECE8;
  const lv_color_t ambient_color = lv_color_hex(ambient_source_color);
  const lv_opa_t ambient_bg_opacity =
      !policy.colored_base
          ? static_cast<lv_opa_t>(LV_OPA_60)
          : static_cast<lv_opa_t>(ambient_effect_base_opacity(
                effect.effect,
                static_cast<float>(global_level) / 255.0F));

  if (!ui.ambient_ready || !lv_color_eq(ui.ambient_color, ambient_color)) {
    lv_obj_set_style_bg_color(ui.ambient, ambient_color, LV_PART_MAIN);
    ui.ambient_color = ambient_color;
  }
  if (!ui.ambient_ready || ui.ambient_bg_opacity != ambient_bg_opacity) {
    lv_obj_set_style_bg_opa(ui.ambient, ambient_bg_opacity, LV_PART_MAIN);
    ui.ambient_bg_opacity = ambient_bg_opacity;
  }

  if (!ui.ambient_strips_ready) {
    ui.ambient_ready = true;
    return;
  }

  if (effect.effect == LightEffect::Snake) {
    const unsigned int phase =
        ambient_strip_phase_index(lighting.ambient_phase);
    const unsigned int mask = ambient_strip_active_tiles(phase);
    const bool phase_changed =
        !ui.ambient_strip_phase_ready || ui.ambient_strip_phase != phase ||
        ui.ambient_strip_effect != effect.effect;
    if (phase_changed) {
      const unsigned int rebuild =
          mask | ui.ambient_strip_visible_mask;
      for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
        const unsigned int bit = 1U << tile;
        if ((rebuild & bit) == 0U) continue;
        if ((mask & bit) != 0U) {
          ambient_strip_fill(begin_ambient_strip_frame(tile), tile, phase);
        } else {
          ambient_strip_clear(begin_ambient_strip_frame(tile), tile);
        }
        commit_ambient_strip_frame(tile);
      }
      ui.ambient_strip_phase = phase;
      ui.ambient_strip_phase_ready = true;
    }
    const lv_color_t motion_color =
        lv_color_hex(ambient_emitter_color(effect.color));
    update_ambient_strip_style(mask, motion_color,
                               static_cast<lv_opa_t>(global_level));
    set_ambient_strip_visible_mask(mask);
  } else if (effect.effect == LightEffect::Rainbow ||
             effect.effect == LightEffect::Gradient) {
    constexpr unsigned int kAllTiles =
        (1U << kAmbientStripTileCount) - 1U;
    if (ui.ambient_strip_effect != effect.effect) {
      for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
        ambient_strip_fill_static(begin_ambient_strip_frame(tile), tile);
        commit_ambient_strip_frame(tile);
      }
    }
    set_ambient_strip_visible_mask(kAllTiles);
    for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
      const unsigned int source = effect_render_color(
          effect.effect, effect.color, ((frame_phase + tile * 8U) & 63U));
      update_ambient_strip_style(1U << tile,
                                 lv_color_hex(ambient_emitter_color(source)),
                                 static_cast<lv_opa_t>(global_level));
    }
  } else {
    const unsigned int clear_mask = ui.ambient_strip_visible_mask;
    for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
      const unsigned int bit = 1U << tile;
      if ((clear_mask & bit) == 0U) continue;
      ambient_strip_clear(begin_ambient_strip_frame(tile), tile);
      commit_ambient_strip_frame(tile);
    }
    set_ambient_strip_visible_mask(0U);
    ui.ambient_strip_phase_ready = false;
  }
  ui.ambient_strip_effect = effect.effect;
  ui.ambient_ready = true;
}

void make_capacitive(lv_obj_t* screen, const Rect& bounds) {
  lv_obj_t* ring = lv_obj_create(screen);
  lv_obj_set_size(ring, 70, 70);
  lv_obj_set_pos(ring, bounds.x + 13, bounds.y + 6);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ring, lv_color_hex(0xE8ECEA), LV_PART_MAIN);
  lv_obj_set_style_border_color(ring, lv_color_hex(0xAEB7B2), LV_PART_MAIN);
  lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(ring, 8, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(ring, LV_OPA_30, LV_PART_MAIN);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* disk = lv_obj_create(ring);
  lv_obj_set_size(disk, 56, 56);
  lv_obj_center(disk);
  lv_obj_set_style_radius(disk, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(disk, lv_color_hex(0x050505), LV_PART_MAIN);
  lv_obj_set_style_border_width(disk, 0, LV_PART_MAIN);
  lv_obj_remove_flag(disk, LV_OBJ_FLAG_SCROLLABLE);
  for (unsigned int index = 0; index < 3; ++index) {
    ui.layer_leds[index] = lv_obj_create(screen);
    lv_obj_set_size(ui.layer_leds[index], 7, 7);
    lv_obj_set_pos(ui.layer_leds[index], bounds.x + 2,
                   bounds.y + layer_led_y_offset(index));
    lv_obj_set_style_radius(ui.layer_leds[index], 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui.layer_leds[index], 0, LV_PART_MAIN);
  }
}

void set_pressed(KeyVisual& visual, bool pressed) {
  if (visual.pressed == pressed) return;
  const int to = visual.resting_y + (pressed ? 3 : 0);
  lv_anim_delete(visual.cap, set_animated_y);
  if (pressed) {
    lv_obj_set_y(visual.cap, to);
  } else {
    animate_y(visual.cap, lv_obj_get_y(visual.cap), to, 45);
  }
  lv_obj_set_style_shadow_offset_y(visual.cap, pressed ? 2 : 6, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(visual.cap, pressed ? LV_OPA_20 : LV_OPA_40,
                              LV_PART_MAIN);
  visual.pressed = pressed;
}

bool same_lighting(const Lighting& left, const Lighting& right) {
  return left.effect == right.effect &&
         left.brightness == right.brightness &&
         left.speed == right.speed &&
         left.magic == right.magic &&
         left.color == right.color;
}

void render_key_lighting(KeyVisual& visual, const Lighting& lighting,
                         float phase, bool agent) {
  const bool on = lighting.effect != LightEffect::Off &&
                  lighting.brightness > 0.0F;
  const unsigned int source_color =
      on && lighting.color == 0 ? 0x304FFE : lighting.color;
  const unsigned int color = on
      ? effect_render_color(lighting.effect, source_color,
                            lighting_phase_index(phase))
      : source_color;
  const float envelope = effect_envelope(lighting.effect, phase);
  const unsigned char brightness_level = static_cast<unsigned char>(
      lighting.brightness <= 0.0F
          ? 0U
          : lighting.brightness >= 1.0F
                ? 255U
                : static_cast<unsigned int>(lighting.brightness * 255.0F));
  const bool configuration_changed =
      !visual.light_ready || visual.light_on != on ||
      visual.light_effect != lighting.effect || visual.light_color != color ||
      visual.light_brightness != brightness_level;
  if (configuration_changed) {
    lv_obj_set_style_image_recolor(visual.glow, lv_color_hex(color),
                                   LV_PART_MAIN);
    visual.breath_level = 0xFF;
    visual.light_on = on;
    visual.light_effect = lighting.effect;
    visual.light_color = color;
    visual.light_brightness = brightness_level;
    if (agent) {
      if (on) {
        // The cap holds a translucent mid-level tint. Only its much smaller
        // inset breathes, avoiding six large gradient invalidations per frame.
        constexpr unsigned char kStaticCapLevel = 8U;
        lv_obj_set_style_bg_color(
            visual.cap,
            lv_color_mix(lv_color_hex(color), lv_color_hex(0xFFFFFF),
                         agent_cap_top_mix(kStaticCapLevel)),
            LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(
            visual.cap,
            lv_color_mix(lv_color_hex(color), lv_color_hex(0xEDF1EF),
                         agent_cap_bottom_mix(kStaticCapLevel)),
            LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            visual.inset,
            lv_color_mix(lv_color_hex(color), lv_color_hex(0xFFFFFF), 104),
            LV_PART_MAIN);
      } else {
        lv_obj_set_style_bg_color(visual.cap, lv_color_hex(theme::kKeyTop),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(
            visual.cap, lv_color_hex(theme::kKeyBottom), LV_PART_MAIN);
        lv_obj_set_style_bg_color(visual.inset, lv_color_hex(0xF8FAF9),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(visual.inset, LV_OPA_70, LV_PART_MAIN);
      }
    }
  }
  const unsigned char breath_level =
      on && agent ? agent_breath_level(lighting.brightness, envelope) : 0U;
  const unsigned char breath_frame = agent_breath_frame(breath_level);
  if (agent && visual.breath_level != breath_frame) {
    if (on) {
      lv_obj_set_style_bg_opa(
          visual.inset,
          static_cast<lv_opa_t>(
              agent_optimized_inset_opacity(breath_frame)),
          LV_PART_MAIN);
    }
    visual.breath_level = breath_frame;
  }
  const lv_opa_t opacity =
      on ? static_cast<lv_opa_t>(
               agent ? agent_static_glow_opacity(lighting.brightness)
                     : brightness_opa(lighting, phase))
         : static_cast<lv_opa_t>(LV_OPA_TRANSP);
  if (!visual.light_ready || visual.light_opacity != opacity) {
    lv_obj_set_style_image_opa(visual.glow, opacity, LV_PART_MAIN);
    visual.light_opacity = opacity;
  }
  visual.light_ready = true;
}

lv_obj_t* make_arcade_mode_button(lv_obj_t* screen, const char* text, int x) {
  lv_obj_t* button = lv_obj_create(screen);
  lv_obj_set_size(button, 112, 42);
  lv_obj_set_pos(button, x, 411);
  lv_obj_set_style_radius(button, 18, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xE9F0ED), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xC4D1CB),
                                LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  theme::make_label(label, 0x26342E, &lv_font_montserrat_12);
  lv_obj_center(label);
  return label;
}

void make_arcade_dpad(lv_obj_t* screen) {
  ui.arcade.dpad = lv_obj_create(screen);
  lv_obj_set_size(ui.arcade.dpad, 196, 196);
  lv_obj_set_pos(ui.arcade.dpad, 247, 122);
  lv_obj_set_style_bg_opa(ui.arcade.dpad, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.arcade.dpad, 0, LV_PART_MAIN);
  lv_obj_remove_flag(ui.arcade.dpad, LV_OBJ_FLAG_SCROLLABLE);

  constexpr short positions[4][2] = {
      {66, 0}, {132, 66}, {66, 132}, {0, 66}};
  constexpr const char* symbols[4] = {
      LV_SYMBOL_UP, LV_SYMBOL_RIGHT, LV_SYMBOL_DOWN, LV_SYMBOL_LEFT};
  for (unsigned int index = 0; index < 4; ++index) {
    lv_obj_t* key = lv_obj_create(ui.arcade.dpad);
    ui.arcade.dpad_keys[index] = key;
    lv_obj_set_size(key, 64, 64);
    lv_obj_set_pos(key, positions[index][0], positions[index][1]);
    lv_obj_set_style_radius(key, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(key, lv_color_hex(0x2D3431), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(key, lv_color_hex(0x090B0A),
                                   LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(key, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_color(key, lv_color_hex(0x68756F),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(key, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(key, 0, LV_PART_MAIN);
    lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* inset = lv_obj_create(key);
    lv_obj_set_size(inset, 52, 52);
    lv_obj_center(inset);
    lv_obj_set_style_radius(inset, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inset, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(inset, lv_color_hex(0x3E4944),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(inset, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(inset, 0, LV_PART_MAIN);
    lv_obj_remove_flag(inset, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* direction = lv_label_create(inset);
    lv_label_set_text(direction, symbols[index]);
    theme::make_label(direction, 0xAAB8B1, &lv_font_montserrat_20);
    lv_obj_center(direction);
  }

  lv_obj_t* center = lv_obj_create(ui.arcade.dpad);
  lv_obj_set_size(center, 48, 48);
  lv_obj_set_pos(center, 74, 74);
  lv_obj_set_style_radius(center, 15, LV_PART_MAIN);
  lv_obj_set_style_bg_color(center, lv_color_hex(0x111513), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(center, lv_color_hex(0x050606),
                                 LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(center, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_width(center, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(center, lv_color_hex(0x59665F),
                                LV_PART_MAIN);
  lv_obj_set_style_shadow_width(center, 0, LV_PART_MAIN);
  lv_obj_remove_flag(center, LV_OBJ_FLAG_SCROLLABLE);
  ui.arcade.dpad_direction = 0xFF;
  lv_obj_add_flag(ui.arcade.dpad, LV_OBJ_FLAG_HIDDEN);
}

void render_arcade_dpad(float angle, float distance) {
  const unsigned char direction = arcade_dpad_direction(angle, distance);
  if (ui.arcade.dpad_direction == direction) return;
  for (unsigned int index = 0; index < 4; ++index) {
    const bool active = direction == index;
    lv_obj_set_style_bg_color(
        ui.arcade.dpad_keys[index],
        lv_color_hex(active ? 0x594927 : 0x2D3431), LV_PART_MAIN);
    lv_obj_set_style_border_color(
        ui.arcade.dpad_keys[index],
        lv_color_hex(active ? 0xF2B84B : 0x68756F), LV_PART_MAIN);
  }
  ui.arcade.dpad_direction = direction;
}

void render_arcade_ambient(const CompositedLighting& lighting) {
  const Lighting& ambient = lighting.ambient;
  const bool on = ambient.effect != LightEffect::Off &&
                  ambient.brightness > 0.0F;
  const unsigned int source_color =
      on ? (ambient.color == 0U ? 0xFF9F0AU : ambient.color) : 0xD9F7EAU;
  const lv_color_t color = lv_color_hex(
      on ? mix_ambient_white(source_color) : source_color);
  const lv_color_t border_color = lv_color_hex(
      on ? ambient_segment_color(ambient.effect, source_color) : 0x71E8B6U);
  const lv_opa_t opacity = on
      ? static_cast<lv_opa_t>(150.0F + ambient.brightness * 95.0F)
      : static_cast<lv_opa_t>(LV_OPA_70);
  if (!ui.arcade.ring_ready ||
      !lv_color_eq(ui.arcade.ring_color, color)) {
    lv_obj_set_style_bg_color(ui.arcade.ring, color, LV_PART_MAIN);
    ui.arcade.ring_color = color;
  }
  if (!ui.arcade.ring_ready ||
      !lv_color_eq(ui.arcade.ring_border_color, border_color)) {
    lv_obj_set_style_border_color(ui.arcade.ring, border_color, LV_PART_MAIN);
    ui.arcade.ring_border_color = border_color;
  }
  if (!ui.arcade.ring_ready || ui.arcade.ring_opacity != opacity) {
    lv_obj_set_style_bg_opa(ui.arcade.ring, opacity, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ui.arcade.ring, opacity, LV_PART_MAIN);
    ui.arcade.ring_opacity = opacity;
  }
  ui.arcade.ring_ready = true;
}

void make_arcade_screen() {
  if (ui.arcade.screen != nullptr) return;
  lv_obj_t* screen = lv_obj_create(nullptr);
  ui.arcade.screen = screen;
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x07100D), LV_PART_MAIN);

  ui.arcade.ring = lv_obj_create(screen);
  lv_obj_set_size(ui.arcade.ring, 468, 468);
  lv_obj_set_pos(ui.arcade.ring, 6, 6);
  lv_obj_set_style_radius(ui.arcade.ring, 62, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.arcade.ring, lv_color_hex(0xD9F7EA), LV_PART_MAIN);
  lv_obj_set_style_border_color(ui.arcade.ring, lv_color_hex(0x71E8B6),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.arcade.ring, 18, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(ui.arcade.ring, 0, LV_PART_MAIN);
  lv_obj_remove_flag(ui.arcade.ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* panel = lv_obj_create(screen);
  lv_obj_set_size(panel, 420, 420);
  lv_obj_set_pos(panel, 30, 30);
  theme::make_panel(panel, 38);
  lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "ARCADE");
  theme::make_label(title, 0x304FFE, &lv_font_montserrat_20);
  lv_obj_set_style_transform_skew_x(title, -12, LV_PART_MAIN);
  lv_obj_set_pos(title, 46, 55);
  ui.arcade.agent =
      make_key(screen, {48, 185, 91, 91}, "+", true, 0xFFFFFF);
  // The classic glow sprite is positioned for the dense 4x4 board. On the
  // sparse Arcade screen its A8 bounds read as a floating blue rectangle, so
  // keep the identical keycap/inset lighting but omit that external sprite.
  lv_obj_add_flag(ui.arcade.agent.glow, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* fire = lv_label_create(screen);
  lv_label_set_text(fire, "Fire in Asteroids");
  theme::make_label(fire, 0x5C6963, &lv_font_montserrat_12);
  lv_obj_set_style_transform_skew_x(fire, -10, LV_PART_MAIN);
  lv_obj_align_to(fire, ui.arcade.agent.cap, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  ui.arcade.joystick_well = lv_obj_create(screen);
  lv_obj_set_size(ui.arcade.joystick_well, 190, 190);
  lv_obj_set_pos(ui.arcade.joystick_well, 250, 125);
  theme::make_keycap(ui.arcade.joystick_well, 50);
  lv_obj_set_style_border_color(ui.arcade.joystick_well,
                                lv_color_hex(0xB1BDB7), LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.arcade.joystick_well, 3, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(ui.arcade.joystick_well, 0, LV_PART_MAIN);
  ui.arcade.joystick_stick = lv_obj_create(ui.arcade.joystick_well);
  lv_obj_set_size(ui.arcade.joystick_stick, 92, 92);
  lv_obj_center(ui.arcade.joystick_stick);
  lv_obj_set_style_radius(ui.arcade.joystick_stick, LV_RADIUS_CIRCLE,
                          LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.arcade.joystick_stick, lv_color_hex(0x242725),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(ui.arcade.joystick_stick,
                                 lv_color_hex(0x020303), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(ui.arcade.joystick_stick, LV_GRAD_DIR_VER,
                               LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.arcade.joystick_stick, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(ui.arcade.joystick_stick,
                                lv_color_hex(0x505A55), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(ui.arcade.joystick_stick, 0, LV_PART_MAIN);
  make_arcade_dpad(screen);

  ui.arcade.touch_region = lv_obj_create(screen);
  lv_obj_set_size(ui.arcade.touch_region, 210, 210);
  lv_obj_set_pos(ui.arcade.touch_region, 240, 115);
  lv_obj_set_style_radius(ui.arcade.touch_region, LV_RADIUS_CIRCLE,
                          LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.arcade.touch_region, lv_color_hex(0xDDE8E3),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ui.arcade.touch_region, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_border_color(ui.arcade.touch_region,
                                lv_color_hex(0x7E9188), LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.arcade.touch_region, 2, LV_PART_MAIN);
  lv_obj_add_flag(ui.arcade.touch_region, LV_OBJ_FLAG_HIDDEN);

  ui.arcade.left_mode_label =
      make_arcade_mode_button(screen, "Use touch area", 215);
  ui.arcade.right_mode_label =
      make_arcade_mode_button(screen, "Use D-pad", 340);
  lv_obj_t* close = lv_obj_create(screen);
  lv_obj_set_size(close, 44, 44);
  lv_obj_set_pos(close, 424, 16);
  lv_obj_set_style_radius(close, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(close, lv_color_hex(0xE9EFEC), LV_PART_MAIN);
  lv_obj_set_style_border_width(close, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(close, lv_color_hex(0xBCC8C2), LV_PART_MAIN);
  lv_obj_t* close_label = lv_label_create(close);
  lv_label_set_text(close_label, "x");
  theme::make_label(close_label, 0x26342E, &lv_font_montserrat_20);
  lv_obj_center(close_label);
  ui.arcade.control_mode = 0;
}

}  // namespace

void ui_init(const DeviceState& initial_state) {
  lv_obj_t* screen = lv_screen_active();
  ui.classic_screen = screen;
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(
      screen, lv_color_hex(kClassicScreenBackdropColor), LV_PART_MAIN);

  ui.ambient = lv_obj_create(screen);
  lv_obj_set_size(ui.ambient, 466, 466);
  lv_obj_set_pos(ui.ambient, 7, 7);
  lv_obj_set_style_radius(ui.ambient, 58, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui.ambient, lv_color_hex(0xE4ECE8), LV_PART_MAIN);
  lv_obj_set_style_border_color(ui.ambient, lv_color_hex(0xF5FAF7), LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.ambient, 2, LV_PART_MAIN);
  // The moving halo is composed from eight small, fixed PSRAM-backed A8
  // tiles. Avoid a 466x466 runtime shadow here: it forces a full-screen
  // software blur whenever the effect opacity changes.
  lv_obj_set_style_shadow_width(ui.ambient, 0, LV_PART_MAIN);
  lv_obj_remove_flag(ui.ambient, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_remove_flag(ui.ambient, LV_OBJ_FLAG_SCROLLABLE);
  make_ambient_strips(screen);

  lv_obj_t* panel = lv_obj_create(screen);
  lv_obj_set_size(panel, 438, 438);
  lv_obj_set_pos(panel, 21, 21);
  theme::make_panel(panel, 40);

  make_screw(screen, 29, 29);
  make_screw(screen, 433, 29);
  make_screw(screen, 29, 433);
  make_screw(screen, 433, 433);
  make_case_typography(screen);

  const ControlLayout layout = make_control_layout();
  make_dial(screen, layout.controls[0].bounds);
  ui.agents[0] = make_key(screen, layout.controls[1].bounds, "+", true, 0xFFFFFF);
  ui.agents[1] = make_key(screen, layout.controls[2].bounds, "+", true, 0xFFFFFF);
  make_joystick(screen, layout.controls[3].bounds);
  for (unsigned int index = 2; index < kAgentCount; ++index) {
    ui.agents[index] = make_key(screen, layout.controls[index + 2].bounds, "+", true,
                                0xFFFFFF);
  }
  const char* command_text[kCommandCount] = {
      "FAST", "APPR", "REJ", "COMPUTER", "MIC", "OAI"};
  for (unsigned int index = 0; index < 4; ++index) {
    ui.commands[index] = make_key(screen, layout.controls[index + 8].bounds,
                                  command_text[index], false, 0xFFFFFF);
  }
  make_capacitive(screen, layout.controls[12].bounds);
  ui.commands[4] = make_key(screen, layout.controls[13].bounds, command_text[4], false, 0xFFFFFF);
  ui.commands[5] = make_key(screen, layout.controls[14].bounds, command_text[5], false, 0xFFFFFF);

  lv_obj_t* footer = lv_label_create(screen);
  lv_label_set_text(footer, "Let's build.");
  theme::make_label(footer, 0x66706B, &lv_font_montserrat_12);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -23);

  make_connection_overlay(screen);

  ui.ready = true;
  ui_apply_snapshot(initial_state, compose_lighting(initial_state, 0));
}

void ui_deinit() {
  // Stop asynchronous state updates before the BSP removes the display and
  // every LVGL object. The module-owned A8 buffers must remain alive until
  // that removal has completed.
  ui.ready = false;
  ui.action_callback = nullptr;
  ui.pointer_callback = nullptr;
}

void ui_release_resources() {
  // Called only after bsp_display_suspend() has removed every LVGL image that
  // referenced these module-owned A8 pixels.
  for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
    for (unsigned int bank = 0; bank < 2U; ++bank) {
      if (ui.ambient_strip_buffers[tile][bank] != nullptr) {
        heap_caps_free(ui.ambient_strip_buffers[tile][bank]);
        ui.ambient_strip_buffers[tile][bank] = nullptr;
      }
    }
  }
  for (unsigned int index = 0; index < kAgentCount; ++index) {
    if (ui.agents[index].custom_icon_pixels != nullptr) {
      heap_caps_free(ui.agents[index].custom_icon_pixels);
      ui.agents[index].custom_icon_pixels = nullptr;
    }
  }
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    if (ui.commands[index].custom_icon_pixels != nullptr) {
      heap_caps_free(ui.commands[index].custom_icon_pixels);
      ui.commands[index].custom_icon_pixels = nullptr;
    }
  }
  ui = UiState{};
}

void ui_set_pointer_callback(UiPointerCallback callback, void* context) {
  ui.pointer_callback = callback;
  ui.pointer_context = context;
}

void ui_set_action_callback(UiActionCallback callback, void* context) {
  ui.action_callback = callback;
  ui.action_context = context;
}

void ui_begin_arcade_entry() {
  if (!ui.ready || ui.arcade.session) return;
  ui.arcade.session = true;
  ui.arcade.active = false;
}

void ui_enter_arcade() {
  if (!ui.ready || !ui.arcade.session) return;
  make_arcade_screen();
  lv_screen_load(ui.arcade.screen);
  ui.arcade.active = true;
  ui_set_arcade_control_mode(0);
}

void ui_begin_arcade_exit() {
  if (!ui.ready || !ui.arcade.session) return;
  ui.arcade.active = false;
}

void ui_finish_arcade_exit() {
  if (!ui.ready || !ui.arcade.session) return;
  lv_screen_load(ui.classic_screen);
  if (ui.arcade.screen != nullptr) lv_obj_delete_async(ui.arcade.screen);
  ui.arcade = {};
  ui.has_snapshot = false;
}

void ui_set_arcade_control_mode(unsigned char mode) {
  if (ui.arcade.screen == nullptr) return;
  if (mode > 2U) mode = 0;
  ui.arcade.control_mode = mode;
  if (mode == 0U) {
    lv_obj_remove_flag(ui.arcade.joystick_well, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.arcade.dpad, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.arcade.touch_region, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui.arcade.left_mode_label, "Use touch area");
    lv_label_set_text(ui.arcade.right_mode_label, "Use D-pad");
  } else if (mode == 1U) {
    lv_obj_add_flag(ui.arcade.joystick_well, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.arcade.dpad, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.arcade.touch_region, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui.arcade.left_mode_label, "Use touch area");
    lv_label_set_text(ui.arcade.right_mode_label, "Use joystick");
  } else {
    lv_obj_add_flag(ui.arcade.joystick_well, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.arcade.dpad, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.arcade.touch_region, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui.arcade.left_mode_label, "Use joystick");
    lv_label_set_text(ui.arcade.right_mode_label, "Use D-pad");
  }
}

bool ui_arcade_active() { return ui.ready && ui.arcade.active; }

void ui_apply_snapshot(const DeviceState& state,
                       const CompositedLighting& lighting) {
  if (!ui.ready) return;
  if (ui.arcade.session) {
    if (ui.arcade.active) {
      render_key_lighting(ui.arcade.agent, lighting.agents[0],
                          lighting.agent_phase[0], true);
      set_pressed(ui.arcade.agent, state.agents[0].pressed);
      render_arcade_ambient(lighting);
      if (ui.arcade.control_mode == 0U) {
        const float radians = state.joystick.angle * 6.28318530718F;
        const int x = static_cast<int>(
            cosf(radians) * state.joystick.distance * 44.0F);
        const int y = static_cast<int>(
            sinf(radians) * state.joystick.distance * 44.0F);
        lv_obj_align(ui.arcade.joystick_stick, LV_ALIGN_CENTER, x, y);
      } else if (ui.arcade.control_mode == 1U) {
        render_arcade_dpad(state.joystick.angle, state.joystick.distance);
      }
    }
    ui.last_state = state;
    ui.last_lighting = lighting;
    return;
  }
  const bool first = !ui.has_snapshot;
  const DeviceState& previous = ui.last_state;
  const CompositedLighting& previous_lighting = ui.last_lighting;

  if (first || state.overlay != previous.overlay ||
      state.transport_connected != previous.transport_connected ||
      state.codex_connected != previous.codex_connected ||
      state.ble_slot != previous.ble_slot ||
      state.transport != previous.transport ||
      state.bluetooth_enabled != previous.bluetooth_enabled ||
      state.sound_volume != previous.sound_volume ||
      state.display_brightness != previous.display_brightness ||
      state.joystick_sensitivity_percent !=
          previous.joystick_sensitivity_percent ||
      state.battery_present != previous.battery_present ||
      state.battery_percent != previous.battery_percent ||
      state.battery_charging != previous.battery_charging ||
      state.usb_power_present != previous.usb_power_present ||
      state.battery_voltage_mv != previous.battery_voltage_mv ||
      state.battery_charge_limit_ma != previous.battery_charge_limit_ma ||
      state.power != previous.power ||
      state.battery_warning_visible != previous.battery_warning_visible ||
      state.standby_timeout_seconds != previous.standby_timeout_seconds ||
      state.smart_screensaver_enabled != previous.smart_screensaver_enabled ||
      state.super_standby_timeout_seconds !=
          previous.super_standby_timeout_seconds ||
      state.anti_accidental_shutdown !=
          previous.anti_accidental_shutdown ||
      state.power_button_mode != previous.power_button_mode ||
      state.auto_ultra_timeout_seconds !=
          previous.auto_ultra_timeout_seconds ||
      state.ultra_touch_wake != previous.ultra_touch_wake) {
    if (state.overlay == Overlay::Connection ||
        state.overlay == Overlay::Pairing) {
      lv_obj_remove_flag(ui.connection_overlay, LV_OBJ_FLAG_HIDDEN);
      const bool slot_bonded = state.ble_slot >= 1 && state.ble_slot <= 3 &&
                               state.ble_peers[state.ble_slot - 1].bonded;
      const char* status =
          state.codex_connected
              ? "connected"
              : state.transport_connected
                    ? "waiting for Codex"
                    : slot_bonded ? "paired, offline" : "ready to pair";
      lv_label_set_text(ui.connection_status, status);
      lv_obj_set_style_bg_color(
          ui.connection_dot,
          lv_color_hex(state.codex_connected ? 0x20C878 : 0x98A39D),
          LV_PART_MAIN);
      lv_obj_set_style_shadow_color(
          ui.connection_dot,
          lv_color_hex(state.codex_connected ? 0x20C878 : 0x98A39D),
          LV_PART_MAIN);
      lv_obj_set_style_shadow_width(ui.connection_dot,
                                    state.codex_connected ? 10 : 0,
                                    LV_PART_MAIN);
      lv_obj_set_style_shadow_opa(ui.connection_dot,
                                  state.codex_connected ? LV_OPA_50
                                                        : LV_OPA_TRANSP,
                                  LV_PART_MAIN);
      ui.updating_settings = true;
      lv_slider_set_value(ui.volume_slider, state.sound_volume / 10,
                          LV_ANIM_OFF);
      lv_label_set_text_fmt(ui.volume_value, "%u%%", state.sound_volume);
      lv_slider_set_value(ui.brightness_slider,
                          state.display_brightness / 10, LV_ANIM_OFF);
      lv_label_set_text_fmt(ui.brightness_value, "%u%%",
                             state.display_brightness);
      lv_slider_set_value(ui.joystick_sensitivity_slider,
                          state.joystick_sensitivity_percent / 10,
                          LV_ANIM_OFF);
      lv_label_set_text_fmt(ui.joystick_sensitivity_value, "%u%%",
                            state.joystick_sensitivity_percent);
      const BatteryDisplayModel battery = make_battery_display(
          state.battery_present, state.battery_percent,
          state.battery_charging, state.usb_power_present,
          state.battery_voltage_mv, state.battery_charge_limit_ma);
      if (!battery.present) {
        lv_label_set_text(
            ui.battery_status,
            battery.external_power ? "No battery | USB" : "No battery");
        lv_label_set_text(ui.battery_details,
                          "Host reports 100%");
      } else {
        const char* status =
            battery.charging
                ? "Charging"
                : battery.external_power ? "USB full" : "Discharging";
        lv_label_set_text_fmt(ui.battery_status, "%u%% | %s",
                              battery.percent, status);
        if (battery.charging && battery.charge_limit_mw != 0U) {
          lv_label_set_text_fmt(
              ui.battery_details, "%u.%03u V | limit <=%u.%02u W",
              battery.voltage_mv / 1000U, battery.voltage_mv % 1000U,
              battery.charge_limit_mw / 1000U,
              (battery.charge_limit_mw % 1000U) / 10U);
        } else if (battery.external_power) {
          lv_label_set_text_fmt(ui.battery_details,
                                "%u.%03u V | Charge complete",
                                battery.voltage_mv / 1000U,
                                battery.voltage_mv % 1000U);
        } else {
          lv_label_set_text_fmt(ui.battery_details,
                                "%u.%03u V | On battery",
                                battery.voltage_mv / 1000U,
                                battery.voltage_mv % 1000U);
        }
      }
      const char* super_label =
          state.super_standby_timeout_seconds == 0
              ? "Off never"
              : state.super_standby_timeout_seconds == 7200
                    ? "Off 2h"
                    : state.super_standby_timeout_seconds == 10800
                          ? "Off 3h"
                          : state.super_standby_timeout_seconds == 18000
                                ? "Off 5h"
                                : "Off 1h";
      lv_label_set_text(ui.super_standby_value, super_label);
      const unsigned int saver = state.standby_timeout_seconds;
      const char* saver_label =
          saver == 0 ? "Screen never"
          : saver == 30 ? "Screen 30s"
          : saver == 60 ? "Screen 1m"
          : saver == 180 ? "Screen 3m"
          : saver == 600 ? "Screen 10m"
          : saver == 1800 ? "Screen 30m"
          : "Screen 1h";
      lv_label_set_text(ui.screensaver_value, saver_label);
      lv_label_set_text(ui.smart_screensaver_value,
                        state.smart_screensaver_enabled ? "Smart screen: On"
                                                       : "Smart screen: Off");
      lv_label_set_text(ui.anti_shutdown_value,
                        state.anti_accidental_shutdown
                            ? "Anti-touch on"
                            : "Anti-touch off");
      lv_label_set_text(
          ui.power_button_mode_value,
          state.power_button_mode == PowerButtonMode::UltraStandby
              ? "PWR: Ultra standby"
              : "PWR: Connected standby");
      const unsigned int ultra = state.auto_ultra_timeout_seconds;
      const char* ultra_label =
          ultra == 0 ? "Auto ultra: Off"
          : ultra == 3600 ? "Auto ultra: 1h"
          : ultra == 10800 ? "Auto ultra: 3h"
          : ultra == 18000 ? "Auto ultra: 5h"
          : ultra == 28800 ? "Auto ultra: 8h"
                           : "Auto ultra: 12h";
      lv_label_set_text(ui.auto_ultra_value, ultra_label);
      lv_label_set_text(ui.ultra_touch_wake_value,
                        state.ultra_touch_wake
                            ? "Ultra touch wake: On"
                            : "Ultra touch wake: Off");
      lv_label_set_text(ui.bluetooth_value,
                        state.bluetooth_enabled ? "Bluetooth on"
                                                : "Bluetooth off");
      lv_obj_set_style_bg_color(
          ui.bluetooth_button,
          lv_color_hex(state.bluetooth_enabled ? 0xBFF6DD : 0xEDF1EF),
          LV_PART_MAIN);
      ui.updating_settings = false;
    } else {
      lv_obj_add_flag(ui.connection_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (state.power == PowerMode::ProtectedUnlock) {
      if (first || previous.power != PowerMode::ProtectedUnlock) {
        lv_slider_set_value(ui.protected_slider, 0, LV_ANIM_OFF);
      }
      lv_obj_remove_flag(ui.protected_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ui.protected_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (state.battery_warning_visible &&
        state.power == PowerMode::Active) {
      lv_label_set_text(
          ui.battery_warning_text,
          state.battery_present ? "Battery below 10%" : "Battery disconnected");
      lv_obj_remove_flag(ui.battery_warning_overlay, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(ui.battery_warning_overlay);
    } else {
      lv_obj_add_flag(ui.battery_warning_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    for (unsigned int index = 0; index < 4; ++index) {
      const bool selected = static_cast<unsigned int>(state.transport) == index;
      lv_obj_set_style_bg_color(ui.mode_buttons[index],
                                lv_color_hex(selected ? 0xBFF6DD : 0xEDF1EF),
                                LV_PART_MAIN);
      if (index < 3) {
        const bool slot_selected =
            (state.transport == TransportMode::Ble ||
             state.transport == TransportMode::Mixed) &&
            state.ble_slot == index + 1U;
        lv_obj_set_style_bg_color(
            ui.slot_buttons[index],
            lv_color_hex(slot_selected ? 0xAFC6FF : 0xEDF1EF),
            LV_PART_MAIN);
      }
    }
  }
  const bool freeze_background = state.overlay == Overlay::Connection;
  if (!freeze_background) render_ambient(lighting);

  if (first || state.joystick.angle != previous.joystick.angle ||
      state.joystick.distance != previous.joystick.distance) {
    const float joystick_radians = state.joystick.angle * 6.28318530718F;
    const int joystick_x = static_cast<int>(cosf(joystick_radians) *
                                            state.joystick.distance * 13.0F);
    const int joystick_y = static_cast<int>(sinf(joystick_radians) *
                                            state.joystick.distance * 13.0F);
    lv_obj_align(ui.joystick_stick, LV_ALIGN_CENTER, joystick_x, joystick_y);
  }
  if (first || state.encoder.accumulated_degrees !=
                   previous.encoder.accumulated_degrees) {
    lv_image_set_src(
        ui.dial_shine,
        &dial_frames[dial_frame_index(state.encoder.accumulated_degrees)]);
  }

  for (unsigned int index = 0; index < kAgentCount; ++index) {
    const Lighting& agent_light = lighting.agents[index];
    if (!freeze_background) {
      render_key_lighting(ui.agents[index], agent_light,
                          lighting.agent_phase[index], true);
    }
    const unsigned int agent_icon_hash =
        state.app_session.active
            ? state.app_icon_hashes[state.layer - 1U][0][index]
            : 0U;
    const unsigned int previous_agent_icon_hash =
        previous.app_session.active
            ? previous.app_icon_hashes[previous.layer - 1U][0][index]
            : 0U;
    const bool agent_icon_revision_changed =
        state.app_icon_revisions[state.layer - 1U][0] !=
        previous.app_icon_revisions[previous.layer - 1U][0];
    if (first || agent_icon_hash != previous_agent_icon_hash ||
        state.layer != previous.layer ||
        state.app_session.active != previous.app_session.active ||
        agent_icon_revision_changed) {
      show_key_content(ui.agents[index], agent_icon_hash, nullptr, "+",
                       agent_icon_revision_changed);
    }
    set_pressed(ui.agents[index], state.agents[index].pressed);
  }
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    const bool key_changed =
        first ||
        !same_lighting(lighting.commands[index],
                       previous_lighting.commands[index]);
    if (!freeze_background &&
        (key_changed ||
         lighting.command_phase[index] !=
             previous_lighting.command_phase[index])) {
      render_key_lighting(ui.commands[index], lighting.commands[index],
                          lighting.command_phase[index], false);
    }
    const unsigned int command_icon_hash =
        state.app_session.active
            ? state.app_icon_hashes[state.layer - 1U][1][index]
            : 0U;
    const unsigned int previous_command_icon_hash =
        previous.app_session.active
            ? previous.app_icon_hashes[previous.layer - 1U][1][index]
            : 0U;
    const bool command_icon_revision_changed =
        state.app_icon_revisions[state.layer - 1U][1] !=
        previous.app_icon_revisions[previous.layer - 1U][1];
    if (first ||
        command_icon_hash != previous_command_icon_hash ||
        state.layer != previous.layer ||
        state.app_session.active != previous.app_session.active ||
        command_icon_revision_changed ||
        strcmp(state.commands[index].keycap_id,
               previous.commands[index].keycap_id) != 0 ||
        strcmp(state.commands[index].label,
               previous.commands[index].label) != 0) {
      show_key_content(ui.commands[index], command_icon_hash,
                       icon_for_keycap(state.commands[index].keycap_id),
                       state.commands[index].label,
                       command_icon_revision_changed);
    }
    set_pressed(ui.commands[index], state.commands[index].pressed);
  }

  if (ui.ble_indicator_dot != nullptr) {
    const bool pairing_on = state.ble_indicator == BleIndicator::Pairing &&
                            lighting.ambient_phase < 0.55F;
    const unsigned int color = state.ble_indicator == BleIndicator::Connected
                                   ? 0x18B66A
                                   : pairing_on ? 0x304FFE : 0xD8E5E0;
    lv_obj_set_style_bg_color(ui.ble_indicator_dot, lv_color_hex(color), LV_PART_MAIN);
  }

  const bool pairing_phase_changed =
      (lighting.ambient_phase < 0.55F) !=
      (previous_lighting.ambient_phase < 0.55F);
  if (first || state.layer != previous.layer ||
      state.overlay != previous.overlay ||
      state.transport != previous.transport ||
      state.ble_slot != previous.ble_slot || pairing_phase_changed) {
    for (unsigned int index = 0; index < 3; ++index) {
      const unsigned char layer_mask = layer_led_mask(state.layer);
      bool on = (layer_mask & (1U << (2U - index))) != 0;
      if (state.overlay == Overlay::Pairing) {
        on = state.transport == TransportMode::Ble &&
             state.ble_slot == index + 1U &&
             lighting.ambient_phase < 0.55F;
      }
      lv_obj_set_style_bg_color(
          ui.layer_leds[index],
          lv_color_hex(on ? (state.overlay == Overlay::Pairing
                                 ? 0x304FFE : 0xB7B33A)
                          : 0xD8E5E0),
          LV_PART_MAIN);
    }
  }
  ui.last_state = state;
  ui.last_lighting = lighting;
  ui.has_snapshot = true;
}

}  // namespace codex
