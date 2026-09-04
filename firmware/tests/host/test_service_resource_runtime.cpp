#include "codex/service_resource_runtime.h"

#include <cstddef>
#include <cstdint>

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) {
  if (!value) ++failures;
}

struct Memory {
  std::uint8_t wire[600]{};
};

bool read_resource(std::uint32_t offset, std::uint8_t* output,
                   std::size_t size, void* context) {
  auto* memory = static_cast<Memory*>(context);
  if (output == nullptr || offset + size > sizeof(memory->wire)) return false;
  for (std::size_t index = 0; index < size; ++index) {
    output[index] = memory->wire[offset + index];
  }
  return true;
}

void make_compatibility(std::uint8_t* wire) {
  for (std::size_t index = 0;
       index < codex::kCompatibilityPackWireSize; ++index) {
    wire[index] = 0;
  }
  wire[0] = 'C';
  wire[1] = 'C';
  wire[2] = 'P';
  wire[3] = '1';
  wire[4] = 1;
  wire[6] = 4;
  wire[8] = 0;
  wire[9] = 16;
  for (unsigned int index = 0; index < 7; ++index) wire[10 + index] = index;
  wire[12] = 4;
}

codex::ServicePackSnapshot protocol_snapshot() {
  codex::ServicePackSnapshot snapshot{};
  snapshot.generation = 8;
  snapshot.reload_mask = 1;
  snapshot.resource_count = 1;
  snapshot.resources[0].type = codex::ServiceResourceType::ProtocolPolicy;
  snapshot.resources[0].resource_id = 17;
  snapshot.resources[0].offset = 100;
  snapshot.resources[0].size = codex::kCompatibilityPackWireSize;
  return snapshot;
}
}  // namespace

extern "C" void mainCRTStartup() {
  Memory memory{};
  make_compatibility(memory.wire + 100);
  const auto snapshot = protocol_snapshot();
  codex::PreparedServiceResources prepared{};
  require(codex::prepare_service_resources(
      snapshot, read_resource, &memory, prepared));
  require(prepared.has_protocol_policy);
  require(prepared.protocol_policy.effect_map[2] == 4);
  require(codex::map_compatibility_effect(2) == 2);
  codex::activate_service_resources(prepared);
  require(codex::map_compatibility_effect(2) == 4);

  memory.wire[100] = 'X';
  require(!codex::prepare_service_resources(
      snapshot, read_resource, &memory, prepared));

  auto unsupported = snapshot;
  unsupported.reload_mask = 8;
  unsupported.resources[0].type = codex::ServiceResourceType::UiResource;
  unsupported.resources[0].size = 4;
  require(!codex::prepare_service_resources(
      unsupported, read_resource, &memory, prepared));

  ExitProcess(failures);
}
