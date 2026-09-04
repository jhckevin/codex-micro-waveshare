#pragma once

#include "codex/device_event.h"

namespace codex {

constexpr unsigned int kCodexReportBodySize = 63;
constexpr unsigned int kCodexPayloadSize = 61;
constexpr unsigned int kCodexMaxMessage = 4096;
constexpr unsigned char kCodexReportId = 6;
constexpr unsigned char kConfigReportId = 7;

struct CodexReport { unsigned char bytes[kCodexReportBodySize]{}; };
struct ReportBatch { CodexReport reports[70]{}; unsigned char count{0}; bool truncated{false}; };

enum class AssembleStatus : unsigned char { Incomplete, Complete, Ignored, Invalid, Overflow };
struct ReportAssembler {
  char message[kCodexMaxMessage + 1]{};
  unsigned int length{0};
  unsigned int last_fragment_ms{0};
};
struct AssemblerResult { AssembleStatus status{AssembleStatus::Incomplete}; unsigned int length{0}; };

struct RpcDispatch {
  bool ok{false};
  bool lighting_update{false};
  char reply[768]{};
  unsigned short reply_length{0};
  DeviceEvent events[8]{};
  unsigned char event_count{0};
};

[[nodiscard]] ReportBatch frame_codex_json(const char* json, unsigned int length);
[[nodiscard]] AssemblerResult consume_codex_report(ReportAssembler& assembler,
                                                   const unsigned char* report,
                                                   unsigned int length,
                                                   unsigned int monotonic_ms = 0);
void dispatch_codex_rpc_into(const char* json,
                             unsigned int length,
                             const DeviceState& state,
                             unsigned int monotonic_ms,
                             RpcDispatch& output);
[[nodiscard]] RpcDispatch dispatch_codex_rpc(const char* json,
                                             unsigned int length,
                                             const DeviceState& state,
                                             unsigned int monotonic_ms);
[[nodiscard]] unsigned int make_codex_hid_json(char* output, unsigned int capacity,
                                               const char* key, unsigned char action,
                                               signed char agent = -1);
[[nodiscard]] unsigned int make_codex_joystick_json(char* output, unsigned int capacity,
                                                    float angle, float distance);

}  // namespace codex
