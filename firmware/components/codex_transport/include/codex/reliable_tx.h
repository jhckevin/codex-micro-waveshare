#pragma once

#include "codex/device_event.h"

namespace codex {

inline constexpr unsigned int kReliableTxCapacity = 32;

enum class PushResult : unsigned char {
  Queued,
  Backpressure,
};

struct ReliableTxRecord {
  ControlId control{ControlId::Agent0};
  unsigned char action{0};
  signed char direction{0};
  signed char agent{-1};
  unsigned int origin_us{0};
};

static_assert(sizeof(ReliableTxRecord) <= 8);

struct ReliableTxQueue {
  ReliableTxRecord records[kReliableTxCapacity]{};
  unsigned char head{0};
  unsigned char tail{0};
  unsigned char count{0};
  unsigned char high_water{0};
  unsigned int backpressure_count{0};
};

[[nodiscard]] constexpr PushResult push_edge(
    ReliableTxQueue& queue, const ReliableTxRecord& record) {
  if (queue.count >= kReliableTxCapacity) {
    ++queue.backpressure_count;
    return PushResult::Backpressure;
  }
  queue.records[queue.tail] = record;
  queue.tail = static_cast<unsigned char>(
      (queue.tail + 1U) % kReliableTxCapacity);
  ++queue.count;
  if (queue.count > queue.high_water) queue.high_water = queue.count;
  return PushResult::Queued;
}

[[nodiscard]] constexpr const ReliableTxRecord* peek_reliable_tx(
    const ReliableTxQueue& queue) {
  return queue.count == 0 ? nullptr : &queue.records[queue.head];
}

constexpr void commit_reliable_tx(ReliableTxQueue& queue) {
  if (queue.count == 0) return;
  queue.head = static_cast<unsigned char>(
      (queue.head + 1U) % kReliableTxCapacity);
  --queue.count;
}

constexpr void clear_reliable_tx(ReliableTxQueue& queue) {
  queue = ReliableTxQueue{};
}

}  // namespace codex
