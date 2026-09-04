#include "codex/compatibility_pack.h"

#include <cstddef>
#include <cstdint>

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace {
unsigned long failures{};
void require(bool value) { if (!value) ++failures; }
bool same(const char* a, const char* b) {
  unsigned int i = 0; while (a[i] && b[i] && a[i] == b[i]) ++i;
  return a[i] == b[i];
}
void make_valid(std::uint8_t* wire) {
  for (std::size_t i = 0; i < codex::kCompatibilityPackWireSize; ++i) wire[i] = 0;
  wire[0]='C'; wire[1]='C'; wire[2]='P'; wire[3]='1'; wire[4]=1;
  wire[5]=1; wire[6]=4; wire[8]=0; wire[9]=16;
  for (unsigned int i=0;i<7;++i) wire[10+i]=i;
  const char from[]="v.oai.newrgb", to[]="v.oai.rgbcfg";
  for(unsigned int i=0;from[i];++i) wire[24+i]=from[i];
  for(unsigned int i=0;to[i];++i) wire[47+i]=to[i];
}
}
extern "C" void mainCRTStartup() {
  std::uint8_t wire[codex::kCompatibilityPackWireSize]{};
  make_valid(wire);
  auto parsed=codex::parse_compatibility_pack(wire,sizeof(wire));
  require(parsed.ok);
  parsed.snapshot.effect_map[2]=4;
  codex::activate_compatibility_snapshot(parsed.snapshot);
  require(codex::map_compatibility_effect(2)==4);
  char method[24]{};
  require(codex::resolve_compatibility_method("v.oai.newrgb",method,sizeof(method)));
  require(same(method,"v.oai.rgbcfg"));
  wire[10]=9;
  require(codex::parse_compatibility_pack(wire,sizeof(wire)).error==
          codex::CompatibilityPackError::InvalidEffect);
  ExitProcess(failures);
}
