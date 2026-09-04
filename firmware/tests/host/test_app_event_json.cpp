#include "codex/app_event_json.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
bool contains(const char* text, const char* needle) {
  for (unsigned int i = 0; text[i]; ++i) {
    unsigned int j = 0;
    while (needle[j] && text[i + j] == needle[j]) ++j;
    if (!needle[j]) return true;
  }
  return false;
}
}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;
  char json[192]{};
  SideEffect key{.type = SideEffectType::SendAppControl,
                 .action = 1,
                 .app_group = ControlGroup::Agent,
                 .app_id = 7};
  unsigned int length = make_app_input_json(json, sizeof(json), key);
  require(length > 0);
  require(contains(json, "\"group\":\"agent\""));
  require(contains(json, "\"id\":7"));
  require(contains(json, "\"action\":1"));
  require(!contains(json, "\"layer\""));

  SideEffect shadow{.type = SideEffectType::SendAppControl,
                    .action = 1,
                    .app_group = ControlGroup::Command,
                    .app_id = 41};
  length = make_app_input_json(json, sizeof(json), shadow);
  require(length > 0);
  require(contains(json, "\"id\":41"));
  shadow.app_id = 42;
  require(make_app_input_json(json, sizeof(json), shadow) == 0);

  SideEffect joystick{.type = SideEffectType::SendAppControl,
                      .angle = 0.625F,
                      .distance = 0.75F,
                      .action = 3,
                      .app_group = ControlGroup::Joystick,
                      .app_id = 12};
  length = make_app_input_json(json, sizeof(json), joystick);
  require(length > 0);
  require(contains(json, "\"angle_milli\":625"));
  require(contains(json, "\"distance_milli\":750"));

  SideEffect invalid{};
  require(make_app_input_json(json, sizeof(json), invalid) == 0);
  ExitProcess(failures);
}
