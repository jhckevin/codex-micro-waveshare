#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "codex/user_content_pack.h"

namespace {

static_assert(codex::kUserContentMaximumBytes == 0x180000U);

void require(bool value) {
  if (!value) std::abort();
}

void write32(std::uint8_t* output, std::uint32_t value) {
  for (unsigned int index = 0; index < 4; ++index) {
    output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

constexpr std::size_t kSize =
    codex::kUserContentHeaderSize + 2 * codex::kUserContentEntrySize +
    2 * codex::kUserContentIconBytes;

void make_valid(std::uint8_t (&wire)[kSize]) {
  std::memset(wire, 0, sizeof(wire));
  std::memcpy(wire, "CMC1", 4);
  wire[4] = 1;
  wire[5] = 2;
  wire[6] = 48;
  wire[7] = 48;
  write32(wire + 8, sizeof(wire));
  std::memcpy(wire + 16, "voice", 5);
  write32(wire + 48, 96);
  write32(wire + 52, codex::kUserContentIconBytes);
  std::memcpy(wire + 56, "terminal", 8);
  write32(wire + 88, 96 + codex::kUserContentIconBytes);
  write32(wire + 92, codex::kUserContentIconBytes);
}

}  // namespace

int main() {
  std::uint8_t wire[kSize]{};
  make_valid(wire);
  auto parsed = codex::parse_user_content_pack(wire, sizeof(wire));
  require(parsed.ok);
  require(parsed.icon_count == 2);
  require(std::strcmp(parsed.icons[0].id, "voice") == 0);
  require(parsed.icons[1].offset == 96 + codex::kUserContentIconBytes);
  parsed = codex::parse_user_content_index(wire, 96, sizeof(wire));
  require(parsed.ok);
  parsed = codex::parse_user_content_index(wire, 95, sizeof(wire));
  require(!parsed.ok);

  make_valid(wire);
  std::memset(wire + 56, 0, codex::kUserContentIdCapacity);
  std::memcpy(wire + 56, "voice", 5);
  parsed = codex::parse_user_content_pack(wire, sizeof(wire));
  require(!parsed.ok);
  require(parsed.error == codex::UserContentPackError::DuplicateIdentifier);

  make_valid(wire);
  write32(wire + 88, 97 + codex::kUserContentIconBytes);
  parsed = codex::parse_user_content_pack(wire, sizeof(wire));
  require(!parsed.ok);
  require(parsed.error == codex::UserContentPackError::InvalidEntry);

  make_valid(wire);
  wire[16] = '<';
  parsed = codex::parse_user_content_pack(wire, sizeof(wire));
  require(!parsed.ok);
  require(parsed.error == codex::UserContentPackError::InvalidIdentifier);

  return 0;
}
