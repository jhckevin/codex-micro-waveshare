#pragma once

namespace codex {

// Stable FNV-1a identifiers keep all six layers in the reducer without
// copying 72 strings on every input event.
constexpr unsigned int icon_id_hash(const char* id) {
  if (id == nullptr || id[0] == '\0') return 0;
  unsigned int hash = 2166136261U;
  for (unsigned int index = 0; id[index] != '\0'; ++index) {
    hash ^= static_cast<unsigned char>(id[index]);
    hash *= 16777619U;
  }
  return hash == 0 ? 1U : hash;
}

}  // namespace codex
