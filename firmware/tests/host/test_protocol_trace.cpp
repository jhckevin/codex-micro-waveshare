#include "codex/protocol_trace.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  ProtocolTrace trace{};
  trace_protocol(trace, {100, ProtocolTraceType::LinkOpen, 0, 0, ""});
  trace_protocol(trace, {110, ProtocolTraceType::HostOutput, 6, 1,
                         "v.oai.rgbcfg"});
  require(protocol_trace_size(trace) == 2);
  const ProtocolTraceRecord* record = protocol_trace_at(trace, 0);
  require(record != nullptr && record->monotonic_ms == 100);
  record = protocol_trace_at(trace, 1);
  require(record != nullptr && record->report_id == 6);
  require(record->method[0] == 'v' && record->method[6] == 'r');

  for (unsigned int index = 0; index < kProtocolTraceCapacity + 4; ++index) {
    trace_protocol(trace, {200 + index, ProtocolTraceType::RpcReply,
                           6, static_cast<unsigned short>(index), "reply"});
  }
  require(protocol_trace_size(trace) == kProtocolTraceCapacity);
  require(trace.overwritten == 6);
  record = protocol_trace_at(trace, kProtocolTraceCapacity - 1);
  require(record != nullptr &&
          record->monotonic_ms == 200 + kProtocolTraceCapacity + 3);
  require(protocol_trace_at(trace, kProtocolTraceCapacity) == nullptr);
  ExitProcess(failures);
}
