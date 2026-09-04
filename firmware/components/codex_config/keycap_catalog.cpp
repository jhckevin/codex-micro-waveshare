#include "codex/keycap_catalog.h"

namespace codex {
namespace {

constexpr const char* kKeycaps[] = {
    "APPR",
    "APPS",
    "BRCH",
    "BUG",
    "CODEX",
    "COMPUTER",
    "DEL",
    "DIFF",
    "DWN",
    "FAST",
    "FAVOURITE",
    "FOLD",
    "GAUGE",
    "GIT",
    "GITHUB",
    "GIT_BRANCH",
    "GIT_COMMIT_VERTICAL",
    "GIT_COMPARE",
    "GIT_COMPARE_ARROWS",
    "GIT_FORK",
    "GIT_GRAPH",
    "GIT_MERGE",
    "GIT_PULL_REQUEST",
    "GIT_PULL_REQUEST_ARROW",
    "GIT_PULL_REQUEST_CLOSED",
    "GIT_PULL_REQUEST_CREATE",
    "GIT_PULL_REQUEST_CREATE_ARROW",
    "GIT_PULL_REQUEST_DRAFT",
    "LAB",
    "MIC",
    "MESSAGE_CIRCLE_PLUS",
    "MIND+",
    "MIND-",
    "MRG",
    "NAV",
    "NEW",
    "OAI",
    "PAINT",
    "PARTY",
    "PLAY",
    "PR",
    "REJ",
    "SEARCH",
    "SETUP",
    "SPARKLES",
    "SPLIT",
    "TERM",
    "TIME",
    "UPL",
    "WORKFLOW",
    "YOLO",
    "YEET",
};

}  // namespace

unsigned int keycap_catalog_size() {
  return sizeof(kKeycaps) / sizeof(kKeycaps[0]);
}

const char* keycap_catalog_id(unsigned int index) {
  return index < keycap_catalog_size() ? kKeycaps[index] : nullptr;
}

}  // namespace codex
