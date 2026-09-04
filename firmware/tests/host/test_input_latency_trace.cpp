#include "codex/device_reducer.h"
#include "codex/reliable_tx.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

unsigned long failures{};

void require(bool value) {
  if (!value) ++failures;
}

extern "C" void mainCRTStartup() {
  codex::DeviceState state = codex::make_default_state();
  codex::DeviceEvent press =
      codex::make_key_pressed(codex::ControlId::Command4);
  press.enqueued_at_us = 123456U;

  const codex::ReduceResult reduced = codex::reduce(state, press);
  require(reduced.effect_count >= 1U);
  require(reduced.effects[0].type == codex::SideEffectType::SendHid);
  require(reduced.effects[0].origin_us == press.enqueued_at_us);

  const codex::ReliableTxRecord record{
      .control = reduced.effects[0].control,
      .action = reduced.effects[0].action,
      .direction = reduced.effects[0].direction,
      .agent = -1,
      .origin_us = reduced.effects[0].origin_us,
  };
  require(record.origin_us == 123456U);
  require(sizeof(codex::ReliableTxRecord) <= 8U);

  ExitProcess(failures);
}
