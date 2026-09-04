#include "codex/reliable_tx.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;

  ReliableTxQueue queue{};
  for (unsigned int index = 0; index < kReliableTxCapacity; ++index) {
    require(push_edge(
                queue,
                {.control = ControlId::Command4,
                 .action = static_cast<unsigned char>(index & 1U)}) ==
            PushResult::Queued);
  }
  require(queue.high_water == kReliableTxCapacity);
  require(push_edge(queue, {.control = ControlId::Encoder, .action = 1}) ==
          PushResult::Backpressure);
  require(queue.backpressure_count == 1);

  for (unsigned int index = 0; index < kReliableTxCapacity; ++index) {
    const ReliableTxRecord* item = peek_reliable_tx(queue);
    require(item != nullptr);
    require(item->control == ControlId::Command4);
    require(item->action == (index & 1U));
    commit_reliable_tx(queue);
  }
  require(peek_reliable_tx(queue) == nullptr);
  require(queue.count == 0);
  require(push_edge(queue, {.control = ControlId::Agent0, .action = 1}) ==
          PushResult::Queued);
  clear_reliable_tx(queue);
  require(queue.count == 0);
  require(queue.high_water == 0);
  require(queue.backpressure_count == 0);

  ExitProcess(failures);
}
