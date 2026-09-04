#include "codex/codex_protocol.h"
#include "codex/compatibility_pack.h"
#include "codex/battery_policy.h"
#include "codex/firmware_version.h"

namespace codex {
namespace {

struct Span { unsigned int begin; unsigned int end; };

void append(char* output, unsigned int capacity, unsigned int& used, const char* text) {
  unsigned int index = 0;
  while (used + 1 < capacity && text[index]) output[used++] = text[index++];
  output[used] = '\0';
}

void append_uint(char* output, unsigned int capacity, unsigned int& used,
                 unsigned int value) {
  char digits[11]{}; unsigned int count = 0;
  do { digits[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value);
  while (count && used + 1 < capacity) output[used++] = digits[--count];
  output[used] = '\0';
}

void append_float(char* output, unsigned int capacity, unsigned int& used, float value) {
  if (value < 0.0F) { append(output, capacity, used, "-"); value = -value; }
  unsigned int whole = static_cast<unsigned int>(value);
  append_uint(output, capacity, used, whole);
  append(output, capacity, used, ".");
  unsigned int fraction = static_cast<unsigned int>((value - whole) * 1000.0F + 0.5F);
  if (fraction < 100) append(output, capacity, used, "0");
  if (fraction < 10) append(output, capacity, used, "0");
  append_uint(output, capacity, used, fraction);
}

bool token_at(const char* data, unsigned int length, unsigned int start,
              const char* token) {
  unsigned int index = 0;
  while (token[index] && start + index < length && data[start + index] == token[index]) ++index;
  return !token[index];
}

bool find(const char* data, Span span, const char* token, unsigned int* position) {
  for (unsigned int index = span.begin; index < span.end; ++index) {
    if (token_at(data, span.end, index, token)) { if (position) *position = index; return true; }
  }
  return false;
}

bool string_value(const char* data, Span span, const char* key,
                  char* output, unsigned int capacity) {
  unsigned int position = 0;
  if (!find(data, span, key, &position)) return false;
  while (position < span.end && data[position] != ':') ++position;
  while (position < span.end && data[position] != '"') ++position;
  if (position >= span.end) return false;
  ++position; unsigned int used = 0;
  while (position < span.end && data[position] != '"') {
    if (used + 1 >= capacity || data[position] == '\\') return false;
    output[used++] = data[position++];
  }
  if (position >= span.end) return false;
  output[used] = '\0'; return true;
}

bool number_value(const char* data, Span span, const char* key, float* output) {
  unsigned int position = 0;
  if (!find(data, span, key, &position)) return false;
  while (position < span.end && data[position] != ':') ++position;
  if (position >= span.end) return false;
  ++position;
  while (position < span.end && (data[position] == ' ' || data[position] == '\t')) ++position;
  bool negative = false;
  if (position < span.end && data[position] == '-') { negative = true; ++position; }
  if (position >= span.end || data[position] < '0' || data[position] > '9') return false;
  float value = 0.0F;
  while (position < span.end && data[position] >= '0' && data[position] <= '9') {
    value = value * 10.0F + static_cast<float>(data[position++] - '0');
  }
  if (position < span.end && data[position] == '.') {
    ++position; float scale = 0.1F;
    while (position < span.end && data[position] >= '0' && data[position] <= '9') {
      value += static_cast<float>(data[position++] - '0') * scale; scale *= 0.1F;
    }
  }
  *output = negative ? -value : value; return true;
}

bool object_after(const char* data, Span span, const char* key, Span* object) {
  unsigned int position = 0;
  if (!find(data, span, key, &position)) return false;
  while (position < span.end && data[position] != '{') ++position;
  if (position >= span.end) return false;
  unsigned int depth = 0; bool in_string = false; bool escaped = false;
  for (unsigned int index = position; index < span.end; ++index) {
    const char ch = data[index];
    if (in_string) { if (escaped) escaped = false; else if (ch == '\\') escaped = true; else if (ch == '"') in_string = false; continue; }
    if (ch == '"') in_string = true;
    else if (ch == '{') ++depth;
    else if (ch == '}' && --depth == 0) { *object = {position, index + 1}; return true; }
  }
  return false;
}

bool next_object(const char* data, Span span, unsigned int* cursor, Span* object) {
  unsigned int position = *cursor;
  while (position < span.end && data[position] != '{') ++position;
  if (position >= span.end) return false;
  Span remaining{position, span.end};
  if (!object_after(data, remaining, "{", object)) return false;
  *cursor = object->end; return true;
}

bool same(const char* a, const char* b) {
  unsigned int i = 0; while (a[i] && b[i] && a[i] == b[i]) ++i; return a[i] == b[i];
}

Lighting parse_lighting(const char* json, Span span) {
  Lighting lighting{}; float value = 0.0F;
  if (number_value(json, span, "\"e\"", &value)) {
    const int effect = static_cast<int>(value);
    lighting.effect = effect >= 0 && effect <= 6
                          ? static_cast<LightEffect>(
                                map_compatibility_effect(effect))
                          : static_cast<LightEffect>(0xFF);
  } else {
    char effect[20]{};
    if (string_value(json, span, "\"e\"", effect, sizeof(effect))) {
      lighting.effect =
          same(effect, "off") ? LightEffect::Off
          : same(effect, "solid") ? LightEffect::Solid
          : same(effect, "snake") ? LightEffect::Snake
          : same(effect, "rainbow") ? LightEffect::Rainbow
          : same(effect, "breath") ? LightEffect::Breath
          : same(effect, "gradient") ? LightEffect::Gradient
          : same(effect, "shallowBreath") ? LightEffect::ShallowBreath
          : static_cast<LightEffect>(0xFF);
    }
  }
  if (number_value(json, span, "\"b\"", &value)) lighting.brightness = value;
  if (number_value(json, span, "\"s\"", &value)) lighting.speed = value;
  if (number_value(json, span, "\"m\"", &value)) {
    lighting.magic = value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
  }
  if (number_value(json, span, "\"c\"", &value)) lighting.color = static_cast<unsigned int>(value);
  if (lighting.effect != LightEffect::Off && lighting.brightness > 0.0F &&
      lighting.color == 0) {
    lighting.color = 0x304FFE;
  }
  return lighting;
}

void success(RpcDispatch& output, const char* id, const char* result) {
  unsigned int used = 0; append(output.reply, sizeof(output.reply), used, "{\"id\":");
  append(output.reply, sizeof(output.reply), used, id); append(output.reply, sizeof(output.reply), used, ",\"result\":");
  append(output.reply, sizeof(output.reply), used, result); append(output.reply, sizeof(output.reply), used, "}\n");
  output.reply_length = static_cast<unsigned short>(used); output.ok = true;
}

void method_error(RpcDispatch& output, const char* id) {
  unsigned int used = 0; append(output.reply, sizeof(output.reply), used, "{\"id\":");
  append(output.reply, sizeof(output.reply), used, id);
  append(output.reply, sizeof(output.reply), used, ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}\n");
  output.reply_length = static_cast<unsigned short>(used);
}

void set_null_id(char* id, unsigned int capacity) {
  if (capacity < 5) {
    if (capacity) id[0] = 0;
    return;
  }
  id[0] = 'n'; id[1] = 'u'; id[2] = 'l'; id[3] = 'l'; id[4] = 0;
}

bool copy_id_value(const char* json, unsigned int length, unsigned int position,
                   char* id, unsigned int capacity) {
  while (position < length &&
         (json[position] == ' ' || json[position] == '\t' ||
          json[position] == '\r' || json[position] == '\n')) {
    ++position;
  }
  if (position >= length || capacity < 2) return false;

  unsigned int used = 0;
  if (json[position] == '"') {
    bool escaped = false;
    do {
      const char ch = json[position++];
      if (used + 1 >= capacity) return false;
      id[used++] = ch;
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"' && used > 1) {
        id[used] = 0;
        return true;
      }
    } while (position < length);
    return false;
  }

  if (token_at(json, length, position, "null")) {
    set_null_id(id, capacity);
    return capacity >= 5;
  }

  if (json[position] == '-') {
    if (used + 1 >= capacity) return false;
    id[used++] = json[position++];
  }
  const unsigned int digit_begin = position;
  while (position < length && json[position] >= '0' && json[position] <= '9') {
    if (used + 1 >= capacity) return false;
    id[used++] = json[position++];
  }
  if (position == digit_begin) return false;
  id[used] = 0;
  return true;
}

void extract_id(const char* json, unsigned int length, char* id, unsigned int capacity) {
  set_null_id(id, capacity);
  unsigned int depth = 0;
  unsigned int position = 0;
  while (position < length) {
    const char ch = json[position];
    if (ch == '{' || ch == '[') {
      ++depth;
      ++position;
      continue;
    }
    if (ch == '}' || ch == ']') {
      if (depth) --depth;
      ++position;
      continue;
    }
    if (ch != '"') {
      ++position;
      continue;
    }

    const unsigned int key_begin = ++position;
    bool escaped = false;
    while (position < length) {
      const char string_ch = json[position];
      if (escaped) {
        escaped = false;
      } else if (string_ch == '\\') {
        escaped = true;
      } else if (string_ch == '"') {
        break;
      }
      ++position;
    }
    if (position >= length) return;
    const unsigned int key_end = position++;
    if (depth != 1) continue;

    unsigned int value = position;
    while (value < length &&
           (json[value] == ' ' || json[value] == '\t' ||
            json[value] == '\r' || json[value] == '\n')) {
      ++value;
    }
    if (value >= length || json[value] != ':') continue;
    ++value;
    const bool is_id = key_end - key_begin == 2 &&
                       json[key_begin] == 'i' && json[key_begin + 1] == 'd';
    const bool is_compact_id =
        key_end - key_begin == 1 && json[key_begin] == 'i';
    if ((is_id || is_compact_id) &&
        copy_id_value(json, length, value, id, capacity)) {
      return;
    }
  }
}

}  // namespace

ReportBatch frame_codex_json(const char* json, unsigned int length) {
  ReportBatch output{};
  unsigned int offset = 0;
  while (offset <= length) {
    if (output.count >= 70) { output.truncated = true; break; }
    CodexReport& report = output.reports[output.count++]; report.bytes[0] = 2;
    unsigned int available = length - offset + 1U;
    unsigned int chunk = available > kCodexPayloadSize ? kCodexPayloadSize : available;
    report.bytes[1] = static_cast<unsigned char>(chunk);
    for (unsigned int index = 0; index < chunk; ++index) {
      report.bytes[index + 2] = offset + index < length ? static_cast<unsigned char>(json[offset + index]) : '\n';
    }
    offset += chunk;
    if (offset > length) break;
  }
  return output;
}

AssemblerResult consume_codex_report(ReportAssembler& assembler,
                                     const unsigned char* report,
                                     unsigned int length,
                                     unsigned int monotonic_ms) {
  if (report == nullptr) return {AssembleStatus::Invalid, assembler.length};
  if (monotonic_ms != 0 && assembler.length != 0 &&
      assembler.last_fragment_ms != 0 &&
      monotonic_ms - assembler.last_fragment_ms > 500U) {
    assembler.length = 0;
  }
  unsigned int offset = length >= 64 && report[0] == kCodexReportId ? 1U : 0U;
  if (length < offset + 2 || report[offset] != 2) return {AssembleStatus::Ignored, assembler.length};
  const unsigned int payload = report[offset + 1];
  if (payload > kCodexPayloadSize || length < offset + 2 + payload) return {AssembleStatus::Invalid, assembler.length};
  const auto* bytes = report + offset + 2;
  if (payload >= 9 && token_at(reinterpret_cast<const char*>(bytes), payload, 0, "{\"method\"") && assembler.length) assembler.length = 0;
  unsigned int start = 0; if (assembler.length == 0) { while (start < payload && bytes[start] != '{') ++start; if (start == payload) return {AssembleStatus::Ignored, 0}; }
  if (assembler.length + payload - start > kCodexMaxMessage) { assembler.length = 0; return {AssembleStatus::Overflow, 0}; }
  for (unsigned int i = start; i < payload; ++i) assembler.message[assembler.length++] = static_cast<char>(bytes[i]);
  if (monotonic_ms != 0) assembler.last_fragment_ms = monotonic_ms;
  assembler.message[assembler.length] = 0;
  unsigned int depth = 0; bool in_string = false; bool escaped = false; bool saw_object = false;
  for (unsigned int i = 0; i < assembler.length; ++i) {
    char ch = assembler.message[i];
    if (in_string) { if (escaped) escaped = false; else if (ch == '\\') escaped = true; else if (ch == '"') in_string = false; continue; }
    if (ch == '"') in_string = true;
    else if (ch == '{') { ++depth; saw_object = true; }
    else if (ch == '}') { if (depth == 0) { assembler.length = 0; return {AssembleStatus::Invalid, 0}; } --depth; }
  }
  if (saw_object && depth == 0) {
    // USB HID and BLE transports use a newline as the message delimiter.  It
    // is framing, not part of the JSON document; strict config/update parsers
    // must receive the exact object bytes.
    while (assembler.length != 0) {
      const char tail = assembler.message[assembler.length - 1];
      if (tail != '\n' && tail != '\r' && tail != ' ' && tail != '\t') break;
      --assembler.length;
    }
    assembler.message[assembler.length] = 0;
    return {AssembleStatus::Complete, assembler.length};
  }
  return {AssembleStatus::Incomplete, assembler.length};
}

void dispatch_codex_rpc_into(const char* json, unsigned int length,
                             const DeviceState& state,
                             unsigned int monotonic_ms,
                             RpcDispatch& output) {
  output.ok = false;
  output.lighting_update = false;
  output.reply[0] = '\0';
  output.reply_length = 0;
  output.event_count = 0;
  if (json == nullptr || length < 2 || length > kCodexMaxMessage || json[0] != '{') return;
  char method[48]{}; char id[32]{};
  if (!string_value(json, {0, length}, "\"method\"", method, sizeof(method))) return;
  char compatible_method[48]{};
  if (resolve_compatibility_method(method, compatible_method,
                                   sizeof(compatible_method))) {
    unsigned int index = 0;
    while (compatible_method[index]) {
      method[index] = compatible_method[index];
      ++index;
    }
    method[index] = '\0';
  }
  extract_id(json, length, id, sizeof(id));
  if (same(method, "sys.version")) {
    success(output, id,
            "{\"version\":\"" CODEX_PUBLIC_FIRMWARE_VERSION "\"}");
    return;
  }
  if (same(method, "device.status")) {
    char result[192]{}; unsigned int used = 0;
    append(result, sizeof(result), used,
           "{\"version\":\"" CODEX_PUBLIC_FIRMWARE_VERSION
           "\",\"profile_index\":0,\"layer_index\":");
    append_uint(result, sizeof(result), used, state.layer);
    append(result, sizeof(result), used, ",\"battery\":");
    append_uint(result, sizeof(result), used,
                state.battery_present ? state.battery_percent : 100);
    append(result, sizeof(result), used, ",\"is_charging\":");
    append(result, sizeof(result), used,
           codex_app_charging_state(state.battery_present,
                                    state.battery_charging,
                                    state.usb_power_present)
               ? "true"
               : "false");
    append(result, sizeof(result), used, "}");
    success(output, id, result); return;
  }
  if (same(method, "v.oai.thstatus")) {
    output.lighting_update = true;
    unsigned int params = 0; if (!find(json, {0, length}, "\"params\"", &params)) return;
    while (params < length && json[params] != '[') ++params;
    unsigned int cursor = params + 1; Span object{};
    while (output.event_count < 8 && next_object(json, {cursor, length}, &cursor, &object)) {
      float agent = -1.0F; if (!number_value(json, object, "\"id\"", &agent)) continue;
      output.events[output.event_count++] = make_agent_lighting_changed(static_cast<unsigned char>(agent), parse_lighting(json, object));
    }
    success(output, id, "{\"ok\":true}"); return;
  }
  if (same(method, "v.oai.rgbcfg")) {
    output.lighting_update = true;
    Span ambient{}, keys{};
    if (object_after(json, {0, length}, "\"ambient\"", &ambient)) output.events[output.event_count++] = make_ambient_lighting_changed(parse_lighting(json, ambient));
    if (object_after(json, {0, length}, "\"keys\"", &keys)) output.events[output.event_count++] = make_keys_lighting_changed(parse_lighting(json, keys));
    success(output, id, "{\"ok\":true}"); return;
  }
  if (same(method, "lights.preview")) {
    output.lighting_update = true;
    Span params{};
    if (object_after(json, {0, length}, "\"params\"", &params)) output.events[output.event_count++] = make_temporary_lighting_changed(parse_lighting(json, params), monotonic_ms + 4000U);
    success(output, id, "{\"ok\":true}"); return;
  }
  if (same(method, "host.focused_app")) { success(output, id, "{\"ok\":true}"); return; }
  method_error(output, id); return;
}

RpcDispatch dispatch_codex_rpc(const char* json, unsigned int length,
                               const DeviceState& state,
                               unsigned int monotonic_ms) {
  RpcDispatch output{};
  dispatch_codex_rpc_into(json, length, state, monotonic_ms, output);
  return output;
}

unsigned int make_codex_hid_json(char* output, unsigned int capacity,
                                 const char* key, unsigned char action,
                                 signed char agent) {
  unsigned int used = 0; append(output, capacity, used, "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"");
  append(output, capacity, used, key); append(output, capacity, used, "\",\"act\":"); append_uint(output, capacity, used, action);
  if (agent >= 0) { append(output, capacity, used, ",\"ag\":"); append_uint(output, capacity, used, static_cast<unsigned int>(agent)); }
  append(output, capacity, used, "}}"); return used;
}

unsigned int make_codex_joystick_json(char* output, unsigned int capacity,
                                      float angle, float distance) {
  unsigned int used = 0; append(output, capacity, used, "{\"method\":\"v.oai.rad\",\"params\":{\"a\":"); append_float(output, capacity, used, angle);
  append(output, capacity, used, ",\"d\":"); append_float(output, capacity, used, distance); append(output, capacity, used, "}}"); return used;
}

}  // namespace codex
