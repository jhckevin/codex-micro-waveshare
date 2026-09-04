#pragma once

#include "lvgl.h"

namespace codex::theme {

constexpr unsigned int kInk = 0x171717;
constexpr unsigned int kPanelTop = 0xF2F5F3;
constexpr unsigned int kPanelBottom = 0xD8DEDA;
constexpr unsigned int kKeyTop = 0xFFFFFF;
constexpr unsigned int kKeyBottom = 0xE3E8E5;
constexpr unsigned int kAgentMark = 0x685FAE;
constexpr unsigned int kMint = 0x68F5B1;

void make_panel(lv_obj_t* object, int radius);
void make_keycap(lv_obj_t* object, int radius);
void make_label(lv_obj_t* label, unsigned int color, const lv_font_t* font);

}  // namespace codex::theme
