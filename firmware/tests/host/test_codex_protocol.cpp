#include "codex/codex_protocol.h"
#include "codex/device_reducer.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool value) { failures += value ? 0UL : 1UL; }
unsigned int length(const char* text) { unsigned int n = 0; while (text[n]) ++n; return n; }
bool contains(const char* text, const char* needle) {
  for (unsigned int i = 0; text[i]; ++i) { unsigned int j = 0; while (needle[j] && text[i+j] == needle[j]) ++j; if (!needle[j]) return true; }
  return false;
}
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  const char outgoing[] = "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"ACT10\",\"act\":1},\"padding\":\"123456789012345678901234567890\"}";
  ReportBatch batch = frame_codex_json(outgoing, length(outgoing));
  require(batch.count == 2);
  require(batch.reports[0].bytes[0] == 2 && batch.reports[0].bytes[1] == 61);

  ReportAssembler assembler{};
  AssemblerResult assembled{};
  for (unsigned int i = 0; i < batch.count; ++i) {
    assembled = consume_codex_report(assembler, batch.reports[i].bytes, 63);
  }
  require(assembled.status == AssembleStatus::Complete);
  require(contains(assembler.message, "v.oai.hid"));
  require(assembler.length == length(outgoing));
  require(assembler.message[assembler.length - 1] == '}');

  ReportAssembler raw_host_assembler{};
  for (unsigned int report_index = 0; report_index < batch.count;
       ++report_index) {
    unsigned char raw_host_report[64]{};
    raw_host_report[0] = kCodexReportId;
    for (unsigned int byte = 0; byte < kCodexReportBodySize; ++byte) {
      raw_host_report[byte + 1] = batch.reports[report_index].bytes[byte];
    }
    assembled = consume_codex_report(raw_host_assembler, raw_host_report,
                                     sizeof(raw_host_report));
  }
  require(assembled.status == AssembleStatus::Complete);
  require(contains(raw_host_assembler.message, "v.oai.hid"));

  ReportAssembler expired{};
  const AssemblerResult first_fragment =
      consume_codex_report(expired, batch.reports[0].bytes, 63, 100);
  require(first_fragment.status == AssembleStatus::Incomplete);
  const AssemblerResult timed_out =
      consume_codex_report(expired, batch.reports[1].bytes, 63, 701);
  require(timed_out.status != AssembleStatus::Complete);

  DeviceState state = make_default_state();
  const char version[] = "{\"id\":7,\"method\":\"sys.version\"}";
  RpcDispatch dispatch = dispatch_codex_rpc(version, length(version), state, 1000);
  require(dispatch.ok && contains(dispatch.reply, "\"version\":\"1.0.1.1\""));
  require(!contains(dispatch.reply, "waveshare-dev"));
  RpcDispatch in_place_dispatch{};
  dispatch_codex_rpc_into(version, length(version), state, 1000,
                          in_place_dispatch);
  require(in_place_dispatch.ok);
  require(contains(in_place_dispatch.reply, "\"version\":\"1.0.1.1\""));

  const char threads[] = "{\"id\":8,\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"e\":4,\"b\":1.0,\"s\":0.4,\"c\":3166206}]}";
  dispatch = dispatch_codex_rpc(threads, length(threads), state, 1000);
  require(dispatch.ok && dispatch.event_count == 1);
  require(dispatch.lighting_update);
  require(dispatch.events[0].payload.agent_lighting.lighting.effect == LightEffect::Breath);
  require(contains(dispatch.reply, "\"id\":8"));

  const char id_after_nested_params[] =
      "{\"method\":\"v.oai.thstatus\",\"params\":["
      "{\"id\":0,\"e\":4,\"b\":1.0,\"s\":0.4,\"c\":3166206}],\"id\":576}";
  dispatch = dispatch_codex_rpc(id_after_nested_params,
                                length(id_after_nested_params), state, 1000);
  require(dispatch.ok && dispatch.event_count == 1);
  require(contains(dispatch.reply, "\"id\":576"));
  require(!contains(dispatch.reply, "\"id\":0,"));

  const char compact_id_after_params[] =
      "{\"method\":\"v.oai.rgbcfg\",\"params\":{"
      "\"ambient\":{\"e\":2,\"b\":1,\"s\":0.4,\"c\":3166206}},\"i\":868}";
  dispatch = dispatch_codex_rpc(compact_id_after_params,
                                length(compact_id_after_params), state, 1000);
  require(dispatch.ok);
  require(contains(dispatch.reply, "\"id\":868"));

  const char string_id_after_params[] =
      "{\"method\":\"host.focused_app\",\"params\":{\"id\":0},"
      "\"id\":\"request-159\"}";
  dispatch = dispatch_codex_rpc(string_id_after_params,
                                length(string_id_after_params), state, 1000);
  require(dispatch.ok);
  require(contains(dispatch.reply, "\"id\":\"request-159\""));

  const char active_without_color[] =
      "{\"id\":81,\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":1,\"e\":4,\"b\":0.8,\"s\":0.4,\"c\":0}]}";
  dispatch = dispatch_codex_rpc(active_without_color,
                                length(active_without_color), state, 1000);
  require(dispatch.ok && dispatch.event_count == 1);
  require(dispatch.events[0].payload.agent_lighting.lighting.color ==
          0x304FFEU);

  const char inactive_without_color[] =
      "{\"id\":82,\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":1,\"e\":0,\"b\":0,\"s\":0,\"c\":0}]}";
  dispatch = dispatch_codex_rpc(inactive_without_color,
                                length(inactive_without_color), state, 1000);
  require(dispatch.ok && dispatch.event_count == 1);
  require(dispatch.events[0].payload.agent_lighting.lighting.effect ==
          LightEffect::Off);
  require(dispatch.events[0].payload.agent_lighting.lighting.color == 0U);

  const char rgb[] = "{\"id\":9,\"method\":\"v.oai.rgbcfg\",\"params\":{\"ambient\":{\"e\":2,\"b\":1,\"s\":0.4,\"m\":0,\"c\":3166206},\"keys\":{\"e\":0,\"b\":0,\"s\":0,\"m\":0,\"c\":0}}}";
  dispatch = dispatch_codex_rpc(rgb, length(rgb), state, 1000);
  require(dispatch.ok && dispatch.event_count == 2);
  require(dispatch.lighting_update);
  require(dispatch.events[0].payload.lighting.effect == LightEffect::Snake);
  require(dispatch.events[0].payload.lighting.color == 3166206U);

  const char fractional_magic[] =
      "{\"id\":92,\"method\":\"v.oai.rgbcfg\",\"params\":{"
      "\"ambient\":{\"e\":2,\"b\":1,\"s\":0.4,\"m\":0.5,\"c\":3166206},"
      "\"keys\":{\"e\":0,\"b\":0,\"s\":0,\"m\":2,\"c\":0}}}";
  dispatch = dispatch_codex_rpc(fractional_magic,
                                length(fractional_magic), state, 1000);
  require(dispatch.ok && dispatch.event_count == 2);
  require(dispatch.events[0].payload.lighting.magic > 0.49F &&
          dispatch.events[0].payload.lighting.magic < 0.51F);
  require(dispatch.events[1].payload.lighting.magic == 1.0F);

  const char negative_magic[] =
      "{\"id\":93,\"method\":\"v.oai.rgbcfg\",\"params\":{"
      "\"ambient\":{\"e\":2,\"b\":1,\"s\":0.4,\"m\":-0.5,\"c\":3166206}}}";
  dispatch =
      dispatch_codex_rpc(negative_magic, length(negative_magic), state, 1000);
  require(dispatch.ok && dispatch.event_count == 1);
  require(dispatch.events[0].payload.lighting.magic == 0.0F);

  const char string_effect[] =
      "{\"id\":91,\"method\":\"v.oai.rgbcfg\",\"params\":{"
      "\"ambient\":{\"e\":\"snake\",\"b\":1,\"s\":0.4,\"c\":3166206},"
      "\"keys\":{\"e\":\"shallowBreath\",\"b\":0.5,\"s\":0.2,\"c\":16777215}}}";
  dispatch =
      dispatch_codex_rpc(string_effect, length(string_effect), state, 1000);
  require(dispatch.ok && dispatch.event_count == 2);
  require(dispatch.events[0].payload.lighting.effect == LightEffect::Snake);
  require(dispatch.events[1].payload.lighting.effect ==
          LightEffect::ShallowBreath);

  state.battery_present = true;
  state.battery_percent = 47;
  state.battery_charging = true;
  const char status_rpc[] = "{\"id\":7,\"method\":\"device.status\"}";
  dispatch =
      dispatch_codex_rpc(status_rpc, length(status_rpc), state, 1001);
  require(dispatch.ok);
  require(contains(dispatch.reply, "\"version\":\"1.0.1.1\""));
  require(contains(dispatch.reply, "\"battery\":47"));
  require(contains(dispatch.reply, "\"is_charging\":true"));
  state.battery_charging = false;
  state.usb_power_present = true;
  dispatch =
      dispatch_codex_rpc(status_rpc, length(status_rpc), state, 1002);
  require(contains(dispatch.reply, "\"is_charging\":true"));
  state.battery_present = false;
  dispatch =
      dispatch_codex_rpc(status_rpc, length(status_rpc), state, 1003);
  require(contains(dispatch.reply, "\"battery\":100"));
  require(contains(dispatch.reply, "\"is_charging\":false"));

  const char unknown[] = "{\"id\":10,\"method\":\"firmware.update\"}";
  dispatch = dispatch_codex_rpc(unknown, length(unknown), state, 1000);
  require(!dispatch.ok && contains(dispatch.reply, "-32601"));
  ExitProcess(failures);
}
