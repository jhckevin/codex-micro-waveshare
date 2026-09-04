#include "codex/dial_frame_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace { unsigned long failures{}; void require(bool v) { failures += v ? 0 : 1; } }

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(kDialFrameCount == 24);
  require(dial_frame_index(0.0F) == 0);
  require(dial_frame_index(15.0F) == 1);
  require(dial_frame_index(359.0F) == 0);
  require(dial_frame_index(-15.0F) == 23);
  ExitProcess(failures);
}
