#include "codex/service_pack.h"

#include <cstddef>
#include <cstdint>

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) {
  if (!value) ++failures;
}
void write32(std::uint8_t* output, std::uint32_t value) {
  for (unsigned int index = 0; index < 4; ++index) {
    output[index] =
        static_cast<std::uint8_t>(value >> (index * 8U));
  }
}
constexpr std::size_t kCount = 2;
constexpr std::size_t kIndexSize =
    codex::kServicePackHeaderSize + kCount * codex::kServicePackEntrySize;
constexpr std::size_t kWireSize = kIndexSize + 5;

void make_valid(std::uint8_t* wire) {
  for (std::size_t index = 0; index < kWireSize; ++index) wire[index] = 0;
  wire[0] = 'C';
  wire[1] = 'S';
  wire[2] = 'R';
  wire[3] = '1';
  wire[4] = 1;
  wire[5] = kCount;
  write32(wire + 8, kWireSize);
  write32(wire + 12, 9);
  write32(wire + 16, 0x3);

  std::uint8_t* first = wire + codex::kServicePackHeaderSize;
  first[0] =
      static_cast<std::uint8_t>(codex::ServiceResourceType::ProtocolPolicy);
  write32(first + 4, 1);
  write32(first + 8, kIndexSize);
  write32(first + 12, 2);
  for (unsigned int index = 0; index < 32; ++index) {
    first[16 + index] = static_cast<std::uint8_t>(index);
  }

  std::uint8_t* second = first + codex::kServicePackEntrySize;
  second[0] =
      static_cast<std::uint8_t>(codex::ServiceResourceType::UiResource);
  write32(second + 4, 2);
  write32(second + 8, kIndexSize + 2);
  write32(second + 12, 3);
  for (unsigned int index = 0; index < 32; ++index) {
    second[16 + index] = static_cast<std::uint8_t>(0x80U + index);
  }
}
}  // namespace

extern "C" void mainCRTStartup() {
  std::uint8_t wire[kWireSize]{};
  make_valid(wire);

  const auto parsed =
      codex::parse_service_pack_index(wire, kIndexSize, kWireSize);
  require(parsed.ok);
  require(parsed.error == codex::ServicePackError::None);
  require(parsed.snapshot.generation == 9);
  require(parsed.snapshot.reload_mask == 0x3);
  require(parsed.snapshot.resource_count == 2);
  require(parsed.snapshot.resources[0].type ==
          codex::ServiceResourceType::ProtocolPolicy);
  require(parsed.snapshot.resources[0].resource_id == 1);
  require(parsed.snapshot.resources[0].offset == kIndexSize);
  require(parsed.snapshot.resources[1].type ==
          codex::ServiceResourceType::UiResource);
  require(parsed.snapshot.resources[1].size == 3);

  // A compatibility pack must never be accepted by the service parser.
  std::uint8_t compatibility[400]{};
  compatibility[0] = 'C';
  compatibility[1] = 'C';
  compatibility[2] = 'P';
  compatibility[3] = '1';
  require(codex::parse_service_pack_index(
              compatibility, sizeof(compatibility), sizeof(compatibility))
              .error == codex::ServicePackError::WrongMagic);

  make_valid(wire);
  write32(wire + codex::kServicePackHeaderSize + 8, kIndexSize + 1);
  require(codex::parse_service_pack_index(wire, kIndexSize, kWireSize).error ==
          codex::ServicePackError::InvalidEntry);

  make_valid(wire);
  write32(wire + codex::kServicePackHeaderSize +
              codex::kServicePackEntrySize + 4,
          1);
  require(codex::parse_service_pack_index(wire, kIndexSize, kWireSize).error ==
          codex::ServicePackError::DuplicateResource);

  make_valid(wire);
  wire[20] = 1;
  require(codex::parse_service_pack_index(wire, kIndexSize, kWireSize).error ==
          codex::ServicePackError::InvalidReserved);

  ExitProcess(failures);
}
