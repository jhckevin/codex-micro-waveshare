#include "codex/update_manifest.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require_at(bool value, int line) {
  if (!value) {
    ++failures;
    std::fprintf(stderr, "require failed at line %d\n", line);
  }
}
#define require(value) require_at((value), __LINE__)
void put16(std::uint8_t* output, std::uint16_t value) {
  output[0] = static_cast<std::uint8_t>(value);
  output[1] = static_cast<std::uint8_t>(value >> 8U);
}
void put32(std::uint8_t* output, std::uint32_t value) {
  for (unsigned int index = 0; index < 4; ++index) {
    output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}
void put64(std::uint8_t* output, std::uint64_t value) {
  for (unsigned int index = 0; index < 8; ++index) {
    output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}
void copy(std::uint8_t* output, const char* value, std::size_t size) {
  for (std::size_t index = 0; index < size; ++index) {
    output[index] = static_cast<std::uint8_t>(value[index]);
  }
}
void make_valid(std::uint8_t* wire) {
  for (std::size_t index = 0; index < codex::kUpdateManifestWireSize;
       ++index) {
    wire[index] = 0;
  }
  copy(wire, "CMU1", 4);
  wire[4] = 1;
  wire[5] = static_cast<std::uint8_t>(
      codex::UpdatePackageClass::Compatibility);
  put16(wire + 6, 0x0003);
  for (unsigned int index = 0; index < 16; ++index) wire[8 + index] = index;
  put64(wire + 24, 42);
  put32(wire + 32, 4096);
  wire[128] = 7;
  copy(wire + 136, "1.2.3.4", 7);
  wire[168] = 70;
  wire[176] = 0x30;
}
}  // namespace

extern "C" void mainCRTStartup() {
  std::uint8_t wire[codex::kUpdateManifestWireSize]{};
  make_valid(wire);
  auto parsed = codex::parse_update_manifest(wire, sizeof(wire));
  require(parsed.ok);
  require(parsed.error == codex::ManifestParseError::None);
  require(parsed.manifest.release_sequence == 42);
  require(parsed.manifest.payload_size == 4096);
  require(parsed.manifest.signature_size == 70);
  require(parsed.manifest.target_version[7] == '\0');

  require(!codex::parse_update_manifest(wire, sizeof(wire) - 1).ok);
  wire[0] = 'X';
  require(codex::parse_update_manifest(wire, sizeof(wire)).error ==
          codex::ManifestParseError::WrongMagic);
  make_valid(wire);
  wire[129] = 1;
  require(codex::parse_update_manifest(wire, sizeof(wire)).error ==
          codex::ManifestParseError::InvalidReserved);
  make_valid(wire);
  wire[136] = '/';
  require(codex::parse_update_manifest(wire, sizeof(wire)).error ==
          codex::ManifestParseError::InvalidVersion);
  make_valid(wire);
  put32(wire + 32, codex::maximum_payload_size(
                           codex::UpdatePackageClass::Compatibility) +
                       1U);
  require(codex::parse_update_manifest(wire, sizeof(wire)).error ==
          codex::ManifestParseError::InvalidPayloadSize);

  ExitProcess(failures);
}
