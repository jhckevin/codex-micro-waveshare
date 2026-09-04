#include "theme.h"

namespace codex::theme {

void make_panel(lv_obj_t* object, int radius) {
  lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, lv_color_hex(kPanelTop), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(object, lv_color_hex(kPanelBottom), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_color(object, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(object, lv_color_hex(0x718079), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, 12, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(object, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void make_keycap(lv_obj_t* object, int radius) {
  lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, lv_color_hex(kKeyTop), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(object, lv_color_hex(kKeyBottom), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_color(object, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(object, lv_color_hex(0x77827D), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, 9, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(object, 6, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(object, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void make_label(lv_obj_t* label, unsigned int color, const lv_font_t* font) {
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

}  // namespace codex::theme
