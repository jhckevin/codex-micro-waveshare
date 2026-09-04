#pragma once

namespace codex {

constexpr unsigned int kSettingsPageCount = 3;

[[nodiscard]] constexpr unsigned int settings_page_after_click(
    unsigned int current_page, bool next) {
  const unsigned int page = current_page < kSettingsPageCount
                                ? current_page
                                : kSettingsPageCount - 1U;
  if (next) {
    return page + 1U < kSettingsPageCount ? page + 1U : page;
  }
  return page == 0 ? 0U : page - 1U;
}

}  // namespace codex
