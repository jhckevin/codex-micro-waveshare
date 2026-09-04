#include "codex/ambient_path_lut.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
int absolute(int value) { return value < 0 ? -value : value; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(kAmbientPathSampleCount == 128);
  require(kAmbientSnakeMoverCount == 5);
  for (unsigned int index = 0; index < kAmbientPathSampleCount; ++index) {
    const AmbientPathPoint point = ambient_path_point(index);
    const AmbientPathPoint next = ambient_path_point(index + 1U);
    require(point.x >= 10 && point.x <= 470);
    require(point.y >= 10 && point.y <= 470);
    require(absolute(next.x - point.x) <= 18);
    require(absolute(next.y - point.y) <= 18);
  }
  require(ambient_tail_phase(4, 0) == 4);
  require(ambient_tail_phase(4, 1) == 0);
  require(ambient_tail_phase(4, 2) == 124);
  require(ambient_tail_phase(4, 4) == 116);
  const AmbientPathPoint halfway =
      ambient_path_interpolated(0U, 128U);
  require(halfway.x == 63);
  require(halfway.y == 11);
  require(halfway.horizontal);
  const AmbientPathPoint safe_right =
      ambient_path_interpolated(31U, 0U);
  require(safe_right.x <= 469 && safe_right.y <= 469);
  ExitProcess(failures);
}
