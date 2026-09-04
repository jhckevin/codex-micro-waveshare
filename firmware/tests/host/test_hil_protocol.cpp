#include "hil_protocol.h"

#include <assert.h>
#include <string.h>

int main() {
  using codex::HilCommandType;

  {
    codex::HilLineAccumulator accumulator;
    const char* first = "@hil met";
    for (const char* cursor = first; *cursor != '\0'; ++cursor) {
      assert(accumulator.push(*cursor) == codex::HilLineState::Collecting);
    }
    assert(accumulator.length() == 8);
    const char* second = "rics\r\n";
    for (const char* cursor = second; cursor[1] != '\0'; ++cursor) {
      assert(accumulator.push(*cursor) == codex::HilLineState::Collecting);
    }
    assert(accumulator.push('\n') == codex::HilLineState::Complete);
    assert(strcmp(accumulator.line(), "@hil metrics") == 0);
    accumulator.reset();
    assert(accumulator.length() == 0);
  }

  {
    const codex::HilCommand command = codex::parse_hil_command("@hil ping");
    assert(command.valid);
    assert(command.type == HilCommandType::Ping);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil touch down 3 120 240");
    assert(command.valid);
    assert(command.type == HilCommandType::Touch);
    assert(command.touch.phase == codex::HilTouchPhase::Down);
    assert(command.touch.track_id == 3);
    assert(command.touch.x == 120);
    assert(command.touch.y == 240);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil touch up 3");
    assert(command.valid);
    assert(command.type == HilCommandType::Touch);
    assert(command.touch.phase == codex::HilTouchPhase::Up);
    assert(command.touch.track_id == 3);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil tap 479 0");
    assert(command.valid);
    assert(command.type == HilCommandType::Tap);
    assert(command.touch.x == 479);
    assert(command.touch.y == 0);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil joystick 359 0.75");
    assert(command.valid);
    assert(command.type == HilCommandType::Joystick);
    assert(command.angle == 359.0F);
    assert(command.distance > 0.749F && command.distance < 0.751F);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil key down mic");
    assert(command.valid);
    assert(command.type == HilCommandType::Key);
    assert(command.key_down);
    assert(command.control == codex::ControlId::Command4);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil power");
    assert(command.valid);
    assert(command.type == HilCommandType::PowerButton);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil ultra");
    assert(command.valid);
    assert(command.type == HilCommandType::UltraStandby);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil wake");
    assert(command.valid);
    assert(command.type == HilCommandType::UltraWake);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil screen read 448 448 32 32");
    assert(command.valid);
    assert(command.type == HilCommandType::ScreenRead);
    assert(command.region.x == 448);
    assert(command.region.y == 448);
    assert(command.region.width == 32);
    assert(command.region.height == 32);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil lighting stress on");
    assert(command.valid);
    assert(command.type == HilCommandType::LightingStress);
    assert(command.enabled);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil ble clear 1");
    assert(command.valid);
    assert(command.type == HilCommandType::ClearBleBonds);
    assert(command.ble_slot == 1);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil ble clear 4");
    assert(!command.valid);
    assert(strcmp(command.error, "invalid BLE slot") == 0);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil screen read 0 0 64 64");
    assert(!command.valid);
    assert(strcmp(command.error, "screen region too large") == 0);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil touch down 0 480 10");
    assert(!command.valid);
    assert(strcmp(command.error, "touch coordinate out of range") == 0);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("v.oai.rgbcfg");
    assert(!command.claimed);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil reboot");
    assert(command.claimed);
    assert(!command.valid);
    assert(strcmp(command.error, "unknown command") == 0);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil snapshot");
    assert(command.valid);
    assert(command.type == HilCommandType::Snapshot);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil metrics");
    assert(command.valid);
    assert(command.type == HilCommandType::Metrics);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil trace");
    assert(command.valid);
    assert(command.type == HilCommandType::Trace);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil screen crc");
    assert(command.valid);
    assert(command.type == HilCommandType::ScreenCrc);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil app connect");
    assert(command.valid);
    assert(command.type == HilCommandType::AppConnect);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil app heartbeat");
    assert(command.valid);
    assert(command.type == HilCommandType::AppHeartbeat);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil app disconnect");
    assert(command.valid);
    assert(command.type == HilCommandType::AppDisconnect);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil transport mixed");
    assert(command.valid);
    assert(command.type == HilCommandType::SetTransport);
    assert(command.transport == codex::TransportMode::Mixed);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil layer 6");
    assert(command.valid);
    assert(command.type == HilCommandType::SetLayer);
    assert(command.layer == 6);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil routing layers 3");
    assert(command.valid);
    assert(command.type == HilCommandType::SetRoutingLayerCount);
    assert(command.layer_count == 3);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil routing layer1 4 2");
    assert(command.valid);
    assert(command.type == HilCommandType::SetLayer1Route);
    assert(command.physical_index == 4);
    assert(command.target_layer == 2);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil routing passthrough 2 agent 0x02");
    assert(command.valid);
    assert(command.type == HilCommandType::SetHigherCodexMask);
    assert(command.layer == 2);
    assert(command.group == codex::ControlGroup::Agent);
    assert(command.mask == 2);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command(
            "@hil lighting app agent 7 4 1 0.5 0 0x304ffe");
    assert(command.valid);
    assert(command.type == HilCommandType::AppKeyLighting);
    assert(command.group == codex::ControlGroup::Agent);
    assert(command.control_id == 7);
    assert(command.lighting.effect == codex::LightEffect::Breath);
    assert(command.lighting.color == 0x304FFE);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil routing snapshot");
    assert(command.valid);
    assert(command.type == HilCommandType::RoutingSnapshot);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil routing layers 1");
    assert(!command.valid);
  }
  {
    const codex::HilCommand command =
        codex::parse_hil_command("@hil routing passthrough 2 ambient 1");
    assert(!command.valid);
  }

  return 0;
}
