#pragma once

#include <cstddef>
#include <cstdint>

#include "codex/update_crypto.h"

namespace codex {

constexpr std::size_t kUpdateChunkCapacity = 512;

enum class UpdateTransport : std::uint8_t { Usb, Ble };
enum class UpdateSessionState : std::uint8_t {
  Idle,
  Receiving,
  AwaitingConfirmation,
  Applying,
  Complete,
  Failed,
};
enum class UpdateSessionError : std::uint8_t {
  None,
  TransportNotAllowed,
  ManifestNotVerified,
  Busy,
  WrongState,
  WrongOffset,
  WrongSequence,
  ChunkTooLarge,
  ChunkDigestMismatch,
  StorageFailure,
  Incomplete,
  ConfirmationRequired,
};

struct UpdateStorage {
  bool (*begin)(const UpdateManifest&, void*){};
  bool (*write)(std::uint32_t, const std::uint8_t*, std::size_t, void*){};
  bool (*finalize)(const UpdateManifest&, void*){};
  void (*cancel)(void*){};
  void* context{};
};

struct UpdateSessionSnapshot {
  UpdateSessionState state{UpdateSessionState::Idle};
  UpdateSessionError error{UpdateSessionError::None};
  UpdatePackageClass package_class{UpdatePackageClass::Compatibility};
  std::uint32_t expected_offset{};
  std::uint32_t total_size{};
  std::uint32_t next_sequence{};
  bool device_confirmation_required{};
  char target_version[kUpdateVersionCapacity]{};
};

class UpdateSession {
 public:
  explicit UpdateSession(UpdateStorage storage) : storage_(storage) {}

  [[nodiscard]] bool begin(const UpdateVerificationResult& verified,
                           UpdateTransport transport);
  [[nodiscard]] bool write_chunk(
      std::uint32_t offset, std::uint32_t sequence,
      const std::uint8_t* data, std::size_t size,
      const std::uint8_t digest[kUpdateDigestSize]);
  [[nodiscard]] bool commit();
  [[nodiscard]] bool confirm();
  void cancel();
  [[nodiscard]] UpdateSessionSnapshot snapshot() const;

 private:
  void fail(UpdateSessionError error);

  UpdateStorage storage_{};
  UpdateManifest manifest_{};
  UpdateSessionState state_{UpdateSessionState::Idle};
  UpdateSessionError error_{UpdateSessionError::None};
  std::uint32_t offset_{};
  std::uint32_t sequence_{};
};

}  // namespace codex
