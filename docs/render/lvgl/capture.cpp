// Documentation-only host renderer. Uses the real firmware UI and generated assets.
// Hardware storage/heap are stubbed; no claim about physical refresh or battery values.
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include "../../../firmware/components/codex_ui/ui.cpp"
namespace codex {
bool read_active_user_content_icon_by_hash(std::uint32_t, std::uint8_t*,std::size_t) {return false;}
}
static uint8_t pixels[480*480*2];
static void flush(lv_display_t* display,const lv_area_t*,uint8_t*) {lv_display_flush_ready(display);}
static void save(const char* name) {
 lv_obj_invalidate(lv_screen_active());
 lv_refr_now(nullptr);
 std::filesystem::create_directories("docs/images/embedded");
 auto path=std::string("docs/images/embedded/")+name+".ppm";
 FILE* f=fopen(path.c_str(),"wb"); fprintf(f,"P6\n480 480\n255\n");
 for(unsigned i=0;i<480*480;i++){
   unsigned v=pixels[i*2]|(pixels[i*2+1]<<8);
   unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),
     (unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
   fwrite(rgb,1,3,f);
 } fclose(f);
}
int main() {
 lv_init();
 auto* d=lv_display_create(480,480);
 lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(d,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(d,flush);
 codex::DeviceState s;
 s.battery_present=true;s.battery_percent=73;s.battery_voltage_mv=4000;
 s.battery_charging=true;s.usb_power_present=true;s.battery_charge_limit_ma=1200;
 s.codex_connected=true;s.usb_codex_ready=true;s.transport_connected=true;
 s.sound_volume=40;
 const char* names[]={"FAST","APPR","REJ","COMPUTER","MIC","OAI"};
 for(int i=0;i<6;i++) snprintf(s.commands[i].keycap_id,32,"%s",names[i]);
 s.ambient={codex::LightEffect::Snake,0.85f,0.4f,0,0x304FFE};
 s.agents[0].lighting={codex::LightEffect::Breath,1,0.4f,0,0x304FFE};
 s.agents[3].lighting={codex::LightEffect::Solid,0.8f,0,0,0x00FF4C};
 auto apply=[&](){codex::ui_apply_snapshot(s,codex::compose_lighting(s,900));};
 codex::ui_init(s); apply();save("classical");
 s.commands[4].pressed=true;apply();save("mic-held");
 s.commands[4].pressed=false;
 s.overlay=codex::Overlay::Connection;apply();save("settings-1");
 codex::set_settings_page(1);save("settings-2");
 codex::set_settings_page(2);save("settings-3");
 codex::set_settings_page(0);
 s.battery_present=false;apply();save("settings-no-battery");
 s.battery_present=true;s.battery_percent=8;s.battery_warning_visible=true;
 s.overlay=codex::Overlay::None;apply();save("battery-warning");
 s.battery_warning_visible=false;s.power=codex::PowerMode::ProtectedUnlock;
 apply();save("protected-unlock");
 s.power=codex::PowerMode::Active;apply();
 codex::ui_begin_arcade_entry();save("arcade-enter");
 codex::ui_enter_arcade();s.ambient.color=0xFF6D00;apply();save("arcade-joystick");
 codex::ui_set_arcade_control_mode(1);apply();save("arcade-dpad");
 codex::ui_set_arcade_control_mode(2);apply();save("arcade-touch");
 codex::ui_begin_arcade_exit();save("arcade-exit");
 return 0;
}
