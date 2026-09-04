#include "codex/device_reducer.h"
#include "codex/lighting_mailbox.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;

  LightingMailbox mailbox{};
  for (unsigned int version = 1; version <= 1000; ++version) {
    Lighting lighting{.effect = LightEffect::Breath,
                      .brightness = 1.0F,
                      .speed = 0.4F,
                      .color = version};
    require(publish_lighting_event(
        mailbox, make_agent_lighting_changed(5, lighting)));
  }

  DeviceEvent output[9]{};
  const MailboxRead first =
      consume_lighting_mailbox(mailbox, output, 9);
  require(first.count == 1);
  require(first.coalesced == 999);
  require(output[0].type == EventType::AgentLightingChanged);
  require(output[0].payload.agent_lighting.index == 5);
  require(output[0].payload.agent_lighting.lighting.color == 1000);

  require(consume_lighting_mailbox(mailbox, output, 9).count == 0);
  require(!publish_lighting_event(mailbox, make_tick(1)));

  require(publish_lighting_event(
      mailbox, make_ambient_lighting_changed(
                   {.effect = LightEffect::Snake,
                    .brightness = 0.8F,
                    .speed = 0.3F,
                    .color = 0x304FFE})));
  require(publish_lighting_event(
      mailbox, make_keys_lighting_changed(
                   {.effect = LightEffect::Solid,
                    .brightness = 0.5F,
                    .color = 0xFFFFFF})));
  const MailboxRead second =
      consume_lighting_mailbox(mailbox, output, 1);
  require(second.count == 1);
  require(consume_lighting_mailbox(mailbox, output, 9).count == 1);

  ExitProcess(failures);
}
