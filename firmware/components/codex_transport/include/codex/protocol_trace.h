#pragma once

namespace codex {

constexpr unsigned int kProtocolTraceCapacity = 32;
constexpr unsigned int kProtocolTraceMethodCapacity = 32;

enum class ProtocolTraceType : unsigned char {
  LinkOpen,
  Authorized,
  Ready,
  AuthenticationFailed,
  Disconnected,
  HostOutput,
  QueueOverflow,
  RpcRequest,
  RpcReply,
  UnknownMethod,
};

struct ProtocolTraceRecord {
  unsigned int monotonic_ms{};
  ProtocolTraceType type{ProtocolTraceType::LinkOpen};
  unsigned char report_id{};
  unsigned short detail{};
  char method[kProtocolTraceMethodCapacity]{};
};

struct ProtocolTrace {
  ProtocolTraceRecord records[kProtocolTraceCapacity]{};
  unsigned char head{};
  unsigned char count{};
  unsigned int overwritten{};
};

inline void trace_protocol(ProtocolTrace& trace,
                           const ProtocolTraceRecord& input) {
  unsigned int index = 0;
  if (trace.count < kProtocolTraceCapacity) {
    index = (static_cast<unsigned int>(trace.head) + trace.count) %
            kProtocolTraceCapacity;
    ++trace.count;
  } else {
    index = trace.head;
    trace.head = static_cast<unsigned char>(
        (static_cast<unsigned int>(trace.head) + 1U) %
        kProtocolTraceCapacity);
    ++trace.overwritten;
  }
  ProtocolTraceRecord& output = trace.records[index];
  output = input;
  output.method[kProtocolTraceMethodCapacity - 1] = '\0';
}

[[nodiscard]] constexpr unsigned int protocol_trace_size(
    const ProtocolTrace& trace) {
  return trace.count;
}

[[nodiscard]] inline const ProtocolTraceRecord* protocol_trace_at(
    const ProtocolTrace& trace, unsigned int logical_index) {
  if (logical_index >= trace.count) return nullptr;
  const unsigned int index =
      (static_cast<unsigned int>(trace.head) + logical_index) %
      kProtocolTraceCapacity;
  return &trace.records[index];
}

}  // namespace codex
