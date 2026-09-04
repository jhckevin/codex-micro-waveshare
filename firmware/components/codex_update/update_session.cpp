#include "codex/update_session.h"

namespace codex {
namespace {

bool digest_equal(const std::uint8_t* left, const std::uint8_t* right,
                  std::size_t size) {
  unsigned int difference = 0;
  for (std::size_t index = 0; index < size; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

bool needs_device_confirmation(UpdatePackageClass package_class) {
  return package_class == UpdatePackageClass::ServiceReload ||
         package_class == UpdatePackageClass::CompleteFirmware;
}

}  // namespace

void UpdateSession::fail(UpdateSessionError error) {
  error_ = error;
  if (error == UpdateSessionError::StorageFailure) {
    state_ = UpdateSessionState::Failed;
  }
}

bool UpdateSession::begin(const UpdateVerificationResult& verified,
                          UpdateTransport transport) {
  if (transport != UpdateTransport::Usb) {
    fail(UpdateSessionError::TransportNotAllowed);
    return false;
  }
  if (!verified.ok) {
    fail(UpdateSessionError::ManifestNotVerified);
    return false;
  }
  if (state_ == UpdateSessionState::Receiving ||
      state_ == UpdateSessionState::AwaitingConfirmation ||
      state_ == UpdateSessionState::Applying) {
    fail(UpdateSessionError::Busy);
    return false;
  }
  if (storage_.begin == nullptr ||
      !storage_.begin(verified.parsed.manifest, storage_.context)) {
    fail(UpdateSessionError::StorageFailure);
    return false;
  }
  manifest_ = verified.parsed.manifest;
  offset_ = 0;
  sequence_ = 0;
  error_ = UpdateSessionError::None;
  state_ = UpdateSessionState::Receiving;
  return true;
}

bool UpdateSession::write_chunk(
    std::uint32_t offset, std::uint32_t sequence,
    const std::uint8_t* data, std::size_t size,
    const std::uint8_t digest[kUpdateDigestSize]) {
  if (state_ != UpdateSessionState::Receiving) {
    fail(UpdateSessionError::WrongState);
    return false;
  }
  if (offset != offset_) {
    fail(UpdateSessionError::WrongOffset);
    return false;
  }
  if (sequence != sequence_) {
    fail(UpdateSessionError::WrongSequence);
    return false;
  }
  if (data == nullptr || digest == nullptr || size == 0 ||
      size > kUpdateChunkCapacity ||
      size > static_cast<std::size_t>(manifest_.payload_size - offset_)) {
    fail(UpdateSessionError::ChunkTooLarge);
    return false;
  }
  std::uint8_t observed_digest[kUpdateDigestSize]{};
  if (!sha256_bytes(data, size, observed_digest) ||
      !digest_equal(observed_digest, digest, sizeof(observed_digest))) {
    fail(UpdateSessionError::ChunkDigestMismatch);
    return false;
  }
  if (storage_.write == nullptr ||
      !storage_.write(offset, data, size, storage_.context)) {
    fail(UpdateSessionError::StorageFailure);
    return false;
  }
  offset_ += static_cast<std::uint32_t>(size);
  ++sequence_;
  error_ = UpdateSessionError::None;
  return true;
}

bool UpdateSession::commit() {
  if (state_ != UpdateSessionState::Receiving) {
    fail(UpdateSessionError::WrongState);
    return false;
  }
  if (offset_ != manifest_.payload_size) {
    fail(UpdateSessionError::Incomplete);
    return false;
  }
  if (needs_device_confirmation(manifest_.package_class)) {
    state_ = UpdateSessionState::AwaitingConfirmation;
    error_ = UpdateSessionError::ConfirmationRequired;
    return false;
  }
  return confirm();
}

bool UpdateSession::confirm() {
  if (state_ != UpdateSessionState::Receiving &&
      state_ != UpdateSessionState::AwaitingConfirmation) {
    fail(UpdateSessionError::WrongState);
    return false;
  }
  if (offset_ != manifest_.payload_size) {
    fail(UpdateSessionError::Incomplete);
    return false;
  }
  state_ = UpdateSessionState::Applying;
  if (storage_.finalize == nullptr ||
      !storage_.finalize(manifest_, storage_.context)) {
    fail(UpdateSessionError::StorageFailure);
    return false;
  }
  state_ = UpdateSessionState::Complete;
  error_ = UpdateSessionError::None;
  return true;
}

void UpdateSession::cancel() {
  if (storage_.cancel != nullptr &&
      state_ != UpdateSessionState::Idle &&
      state_ != UpdateSessionState::Complete) {
    storage_.cancel(storage_.context);
  }
  state_ = UpdateSessionState::Idle;
  error_ = UpdateSessionError::None;
  offset_ = 0;
  sequence_ = 0;
  manifest_ = {};
}

UpdateSessionSnapshot UpdateSession::snapshot() const {
  UpdateSessionSnapshot result{};
  result.state = state_;
  result.error = error_;
  result.package_class = manifest_.package_class;
  result.expected_offset = offset_;
  result.total_size = manifest_.payload_size;
  result.next_sequence = sequence_;
  result.device_confirmation_required =
      needs_device_confirmation(manifest_.package_class);
  for (std::size_t index = 0; index < kUpdateVersionCapacity; ++index) {
    result.target_version[index] = manifest_.target_version[index];
  }
  return result;
}

}  // namespace codex
