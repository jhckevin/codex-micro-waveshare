#include "codex/update_session.h"

#include <cstddef>
#include <cstdint>

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { if (!value) ++failures; }
struct MemoryStorage {
  std::uint8_t bytes[1024]{};
  std::size_t size{};
  bool finalized{};
  bool cancelled{};
};
bool begin(const codex::UpdateManifest&, void* context) {
  auto& memory = *static_cast<MemoryStorage*>(context);
  memory.size = 0; memory.finalized = false; memory.cancelled = false;
  return true;
}
bool write(std::uint32_t offset, const std::uint8_t* data, std::size_t size,
           void* context) {
  auto& memory = *static_cast<MemoryStorage*>(context);
  if (offset != memory.size || offset + size > sizeof(memory.bytes)) return false;
  for (std::size_t index = 0; index < size; ++index) {
    memory.bytes[offset + index] = data[index];
  }
  memory.size += size;
  return true;
}
bool finalize(const codex::UpdateManifest&, void* context) {
  static_cast<MemoryStorage*>(context)->finalized = true;
  return true;
}
void cancel(void* context) {
  static_cast<MemoryStorage*>(context)->cancelled = true;
}
codex::UpdateVerificationResult verified(codex::UpdatePackageClass package_class,
                                         std::uint32_t size) {
  codex::UpdateVerificationResult result{};
  result.ok = true; result.parsed.ok = true;
  result.parsed.manifest.package_class = package_class;
  result.parsed.manifest.payload_size = size;
  result.parsed.manifest.target_version[0] = '2';
  return result;
}
}  // namespace

extern "C" void mainCRTStartup() {
  MemoryStorage memory{};
  codex::UpdateSession session({begin, write, finalize, cancel, &memory});
  require(!session.begin(verified(codex::UpdatePackageClass::Compatibility, 3),
                         codex::UpdateTransport::Ble));
  require(session.snapshot().error ==
          codex::UpdateSessionError::TransportNotAllowed);
  require(session.begin(verified(codex::UpdatePackageClass::Compatibility, 3),
                        codex::UpdateTransport::Usb));
  const std::uint8_t data[] = {'a', 'b', 'c'};
  std::uint8_t digest[32]{};
  require(codex::sha256_bytes(data, sizeof(data), digest));
  require(!session.write_chunk(1, 0, data, sizeof(data), digest));
  require(session.snapshot().expected_offset == 0);
  require(session.write_chunk(0, 0, data, sizeof(data), digest));
  require(session.snapshot().expected_offset == 3);
  require(session.commit());
  require(memory.finalized);
  require(session.snapshot().state == codex::UpdateSessionState::Complete);

  session.cancel();
  require(session.begin(
      verified(codex::UpdatePackageClass::CompleteFirmware, 3),
      codex::UpdateTransport::Usb));
  require(session.write_chunk(0, 0, data, sizeof(data), digest));
  require(!session.commit());
  require(session.snapshot().state ==
          codex::UpdateSessionState::AwaitingConfirmation);
  require(session.confirm());
  require(memory.finalized);

  session.cancel();
  require(session.begin(verified(codex::UpdatePackageClass::Compatibility, 6),
                        codex::UpdateTransport::Usb));
  require(session.write_chunk(0, 0, data, sizeof(data), digest));
  require(!session.commit());
  require(session.snapshot().error == codex::UpdateSessionError::Incomplete);
  session.cancel();
  require(memory.cancelled);

  ExitProcess(failures);
}
