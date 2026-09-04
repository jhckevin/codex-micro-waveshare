#include "codex/settings_paging.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(kSettingsPageCount == 3);
  require(settings_page_after_click(0, false) == 0);
  require(settings_page_after_click(0, true) == 1);
  require(settings_page_after_click(1, false) == 0);
  require(settings_page_after_click(1, true) == 2);
  require(settings_page_after_click(2, false) == 1);
  require(settings_page_after_click(2, true) == 2);
  require(settings_page_after_click(7, false) == 1);
  require(settings_page_after_click(7, true) == 2);
  ExitProcess(failures);
}
