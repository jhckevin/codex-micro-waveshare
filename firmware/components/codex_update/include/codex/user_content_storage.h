#pragma once

#include <cstddef>
#include <cstdint>

#include "codex/update_session.h"
#include "codex/user_content_pack.h"
#include "codex/user_content_protocol.h"

namespace codex {

[[nodiscard]] UpdateStorage make_user_content_update_storage();
[[nodiscard]] bool load_active_user_content_index(
    UserContentPackResult& output);
[[nodiscard]] bool read_active_user_content_icon(
    const char* id, std::uint8_t* output, std::size_t output_size);
[[nodiscard]] bool read_active_user_content_icon_by_hash(
    std::uint32_t id_hash, std::uint8_t* output,
    std::size_t output_size);
[[nodiscard]] bool begin_user_content_upload(
    std::uint32_t total_size,
    const std::uint8_t digest[kUserContentDigestBytes]);
[[nodiscard]] bool write_user_content_upload(
    std::uint32_t offset, const std::uint8_t* data, std::size_t size);
[[nodiscard]] bool commit_user_content_upload();
void cancel_user_content_upload();
[[nodiscard]] UserContentTransferSnapshot user_content_upload_snapshot();

}  // namespace codex
