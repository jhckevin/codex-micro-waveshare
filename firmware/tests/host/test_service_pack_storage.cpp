#include "codex/service_pack_storage.h"

#include "codex/update_crypto.h"

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

struct MemoryBackend {
  std::uint8_t slots[2][1024]{};
  std::uint8_t active{};
  bool activated{};
  bool prepared{};
  bool applied{};
  bool reject_prepare{};
  std::uint32_t activated_size{};
  std::uint64_t activated_sequence{};
};

bool erase_slot(std::uint8_t slot, std::uint32_t size, void* context) {
  auto* memory = static_cast<MemoryBackend*>(context);
  if (slot > 1 || size > sizeof(memory->slots[slot])) return false;
  for (std::size_t index = 0; index < sizeof(memory->slots[slot]); ++index) {
    memory->slots[slot][index] = 0;
  }
  return true;
}
bool write_slot(std::uint8_t slot, std::uint32_t offset,
                const std::uint8_t* data, std::size_t size, void* context) {
  auto* memory = static_cast<MemoryBackend*>(context);
  if (slot > 1 || data == nullptr ||
      offset + size > sizeof(memory->slots[slot])) {
    return false;
  }
  for (std::size_t index = 0; index < size; ++index) {
    memory->slots[slot][offset + index] = data[index];
  }
  return true;
}
bool read_slot(std::uint8_t slot, std::uint32_t offset, std::uint8_t* data,
               std::size_t size, void* context) {
  auto* memory = static_cast<MemoryBackend*>(context);
  if (slot > 1 || data == nullptr ||
      offset + size > sizeof(memory->slots[slot])) {
    return false;
  }
  for (std::size_t index = 0; index < size; ++index) {
    data[index] = memory->slots[slot][offset + index];
  }
  return true;
}
bool hash_slot(std::uint8_t slot, std::uint32_t offset, std::uint32_t size,
               std::uint8_t output[codex::kUpdateDigestSize],
               void* context) {
  auto* memory = static_cast<MemoryBackend*>(context);
  if (slot > 1 || offset + size > sizeof(memory->slots[slot])) return false;
  return codex::sha256_bytes(memory->slots[slot] + offset, size, output);
}
std::uint8_t active_slot(void* context) {
  return static_cast<MemoryBackend*>(context)->active;
}
bool activate_slot(std::uint8_t slot, std::uint32_t size,
                   std::uint64_t sequence, void* context) {
  auto* memory = static_cast<MemoryBackend*>(context);
  memory->active = slot;
  memory->activated = true;
  memory->activated_size = size;
  memory->activated_sequence = sequence;
  return true;
}
bool prepare_resources(std::uint8_t slot,
                       const codex::ServicePackSnapshot& snapshot,
                       void* context) {
  auto* memory = static_cast<MemoryBackend*>(context);
  require(!memory->activated);
  require(slot <= 1);
  require(snapshot.resource_count == 1);
  memory->prepared = true;
  return !memory->reject_prepare;
}
void apply_resources(std::uint8_t slot,
                     const codex::ServicePackSnapshot& snapshot,
                     void* context) {
  auto* memory = static_cast<MemoryBackend*>(context);
  require(memory->activated);
  require(memory->prepared);
  require(slot == memory->active);
  require(snapshot.generation == 3);
  memory->applied = true;
}

codex::ServicePackStorageBackend backend(MemoryBackend* memory) {
  return {
      erase_slot,
      write_slot,
      read_slot,
      hash_slot,
      active_slot,
      activate_slot,
      memory,
      prepare_resources,
      apply_resources,
  };
}

constexpr std::size_t kIndexSize =
    codex::kServicePackHeaderSize + codex::kServicePackEntrySize;
constexpr std::size_t kPayloadSize = kIndexSize + 4;

void make_pack(std::uint8_t* wire, bool correct_resource_digest) {
  for (std::size_t index = 0; index < kPayloadSize; ++index) wire[index] = 0;
  wire[0] = 'C';
  wire[1] = 'S';
  wire[2] = 'R';
  wire[3] = '1';
  wire[4] = 1;
  wire[5] = 1;
  write32(wire + 8, kPayloadSize);
  write32(wire + 12, 3);
  write32(wire + 16, 1);
  std::uint8_t* entry = wire + codex::kServicePackHeaderSize;
  entry[0] =
      static_cast<std::uint8_t>(codex::ServiceResourceType::ProtocolPolicy);
  write32(entry + 4, 7);
  write32(entry + 8, kIndexSize);
  write32(entry + 12, 4);
  wire[kIndexSize] = 1;
  wire[kIndexSize + 1] = 2;
  wire[kIndexSize + 2] = 3;
  wire[kIndexSize + 3] = 4;
  std::uint8_t digest[codex::kUpdateDigestSize]{};
  require(codex::sha256_bytes(wire + kIndexSize, 4, digest));
  for (unsigned int index = 0; index < codex::kUpdateDigestSize; ++index) {
    entry[16 + index] =
        correct_resource_digest ? digest[index]
                                : static_cast<std::uint8_t>(digest[index] ^ 0xffU);
  }
}

codex::UpdateManifest manifest_for(const std::uint8_t* wire,
                                   codex::UpdatePackageClass package_class) {
  codex::UpdateManifest manifest{};
  manifest.package_class = package_class;
  manifest.payload_size = kPayloadSize;
  manifest.release_sequence = 12;
  require(codex::sha256_bytes(wire, kPayloadSize, manifest.payload_sha256));
  return manifest;
}
}  // namespace

extern "C" void mainCRTStartup() {
  std::uint8_t wire[kPayloadSize]{};
  make_pack(wire, true);
  auto manifest =
      manifest_for(wire, codex::UpdatePackageClass::ServiceReload);
  MemoryBackend memory{};
  codex::ServicePackInstaller installer(backend(&memory));
  require(installer.begin(manifest));
  require(installer.write(0, wire, 37));
  require(installer.write(37, wire + 37, kPayloadSize - 37));
  require(installer.finalize(manifest));
  require(memory.activated);
  require(memory.active == 1);
  require(memory.activated_size == kPayloadSize);
  require(memory.activated_sequence == 12);
  require(memory.prepared);
  require(memory.applied);
  require(codex::service_snapshot().generation == 3);

  MemoryBackend wrong_class_memory{};
  codex::ServicePackInstaller wrong_class(backend(&wrong_class_memory));
  auto compatibility =
      manifest_for(wire, codex::UpdatePackageClass::Compatibility);
  require(!wrong_class.begin(compatibility));
  require(!wrong_class_memory.activated);

  make_pack(wire, false);
  auto bad_digest_manifest =
      manifest_for(wire, codex::UpdatePackageClass::ServiceReload);
  MemoryBackend bad_digest_memory{};
  codex::ServicePackInstaller bad_digest(backend(&bad_digest_memory));
  require(bad_digest.begin(bad_digest_manifest));
  require(bad_digest.write(0, wire, kPayloadSize));
  require(!bad_digest.finalize(bad_digest_manifest));
  require(!bad_digest_memory.activated);

  make_pack(wire, true);
  MemoryBackend rejected_memory{};
  rejected_memory.reject_prepare = true;
  codex::ServicePackInstaller rejected(backend(&rejected_memory));
  require(rejected.begin(manifest_for(
      wire, codex::UpdatePackageClass::ServiceReload)));
  require(rejected.write(0, wire, kPayloadSize));
  require(!rejected.finalize(manifest_for(
      wire, codex::UpdatePackageClass::ServiceReload)));
  require(rejected_memory.prepared);
  require(!rejected_memory.activated);
  require(!rejected_memory.applied);

  make_pack(wire, true);
  MemoryBackend canceled_memory{};
  codex::ServicePackInstaller canceled(backend(&canceled_memory));
  require(canceled.begin(manifest_for(
      wire, codex::UpdatePackageClass::ServiceReload)));
  canceled.cancel();
  require(!canceled.write(0, wire, kPayloadSize));
  require(!canceled_memory.activated);

  ExitProcess(failures);
}
