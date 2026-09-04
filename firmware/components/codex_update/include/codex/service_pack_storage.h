#pragma once

#include <cstddef>
#include <cstdint>

#include "codex/service_pack.h"
#include "codex/update_manifest.h"

namespace codex {

struct ServicePackStorageBackend {
  bool (*erase)(std::uint8_t slot, std::uint32_t size, void* context){};
  bool (*write)(std::uint8_t slot, std::uint32_t offset,
                const std::uint8_t* data, std::size_t size, void* context){};
  bool (*read)(std::uint8_t slot, std::uint32_t offset, std::uint8_t* data,
               std::size_t size, void* context){};
  bool (*hash)(std::uint8_t slot, std::uint32_t offset, std::uint32_t size,
               std::uint8_t output[32], void* context){};
  std::uint8_t (*active_slot)(void* context){};
  bool (*activate)(std::uint8_t slot, std::uint32_t size,
                   std::uint64_t release_sequence, void* context){};
  void* context{};
  // prepare runs after all digests and the service directory are valid, but
  // before the persistent A/B slot is switched. apply is non-failing and runs
  // only after the new slot became durable.
  bool (*prepare)(std::uint8_t slot, const ServicePackSnapshot& snapshot,
                  void* context){};
  void (*apply)(std::uint8_t slot, const ServicePackSnapshot& snapshot,
                void* context){};
};

class ServicePackInstaller {
 public:
  explicit ServicePackInstaller(ServicePackStorageBackend backend);

  [[nodiscard]] bool begin(const UpdateManifest& manifest);
  [[nodiscard]] bool write(std::uint32_t offset, const std::uint8_t* data,
                           std::size_t size);
  [[nodiscard]] bool finalize(const UpdateManifest& manifest);
  void cancel();

 private:
  ServicePackStorageBackend backend_{};
  std::uint8_t target_slot_{};
  std::uint32_t expected_size_{};
  std::uint32_t written_{};
  std::uint64_t release_sequence_{};
  std::uint8_t expected_digest_[32]{};
  bool open_{};
};

}  // namespace codex
