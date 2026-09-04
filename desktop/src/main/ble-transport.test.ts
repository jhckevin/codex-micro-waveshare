import { PassThrough } from "node:stream";
import { describe, expect, it } from "vitest";

import {
  CODEX_CONFIG_RX_UUID,
  CODEX_CONFIG_SERVICE_UUID,
  CODEX_CONFIG_TX_UUID,
  CodexBleTransport,
  NativeBleProcessChannel,
  type NativeBleProcess,
  type NativeBleSpawn
} from "./ble-transport";

class FakeBleProcess implements NativeBleProcess {
  readonly stdin = new PassThrough();
  readonly stdout = new PassThrough();
  readonly stderr = new PassThrough();
  killed = false;

  kill(): boolean {
    this.killed = true;
    return true;
  }
}

function processHarness(): {
  process: FakeBleProcess;
  spawn: NativeBleSpawn;
  commands: Array<Record<string, unknown>>;
} {
  const process = new FakeBleProcess();
  const commands: Array<Record<string, unknown>> = [];
  let buffered = "";
  process.stdin.on("data", (chunk) => {
    buffered += chunk.toString("utf8");
    let newline = buffered.indexOf("\n");
    while (newline >= 0) {
      const line = buffered.slice(0, newline);
      buffered = buffered.slice(newline + 1);
      if (line.length > 0) commands.push(JSON.parse(line));
      newline = buffered.indexOf("\n");
    }
  });
  const spawn: NativeBleSpawn = () => process;
  return { process, spawn, commands };
}

async function nextTurn(): Promise<void> {
  await new Promise<void>((resolve) => setImmediate(resolve));
}

describe("NativeBleProcessChannel", () => {
  it("opens only the fixed config GATT service and encrypted characteristics", async () => {
    const harness = processHarness();
    const opening = NativeBleProcessChannel.open(
      "C:\\Program Files\\Codex Micro\\native\\codex-ble-helper.exe",
      harness.spawn,
      "C:\\Program Files\\Codex Micro\\native"
    );
    await nextTurn();

    expect(harness.commands).toEqual([{
      id: 1,
      op: "open",
      service_uuid: CODEX_CONFIG_SERVICE_UUID,
      rx_uuid: CODEX_CONFIG_RX_UUID,
      tx_uuid: CODEX_CONFIG_TX_UUID,
      require_encryption: true
    }]);

    harness.process.stdout.write('{"id":1,"ok":true}\n');
    await expect(opening).resolves.toBeInstanceOf(NativeBleProcessChannel);
  });

  it("rejects arbitrary helper locations", async () => {
    const harness = processHarness();
    await expect(NativeBleProcessChannel.open(
      "C:\\Temp\\other.exe",
      harness.spawn,
      "C:\\Program Files\\Codex Micro\\native"
    )).rejects.toThrow("Invalid BLE helper path");
    expect(harness.commands).toEqual([]);
  });
});

describe("CodexBleTransport", () => {
  it("reassembles newline-delimited replies and isolates private input events", async () => {
    const harness = processHarness();
    const opening = NativeBleProcessChannel.open(
      "C:\\Program Files\\Codex Micro\\native\\codex-ble-helper.exe",
      harness.spawn,
      "C:\\Program Files\\Codex Micro\\native"
    );
    await nextTurn();
    harness.process.stdout.write('{"id":1,"ok":true}\n');
    const channel = await opening;
    const transport = new CodexBleTransport(channel);
    const events: unknown[] = [];
    transport.onEvent((event) => events.push(event));

    const response = transport.request({ method: "cfg.get_state" });
    await nextTurn();
    expect(harness.commands[1]).toMatchObject({ id: 2, op: "write" });
    const payload = Buffer.from(
      String(harness.commands[1].value_base64),
      "base64"
    ).toString("utf8");
    expect(payload).toBe('{"method":"cfg.get_state"}\n');
    harness.process.stdout.write('{"id":2,"ok":true}\n');

    harness.process.stdout.write(
      `${JSON.stringify({
        event: "notification",
        value_base64: Buffer.from(
          '{"event":"input","group":"agent","id":7,"action":0}\n{"ok":tr'
        ).toString("base64")
      })}\n`
    );
    harness.process.stdout.write(
      `${JSON.stringify({
        event: "notification",
        value_base64: Buffer.from("ue,\"protocol\":1}\n").toString("base64")
      })}\n`
    );

    await expect(response).resolves.toEqual({ ok: true, protocol: 1 });
    expect(events).toEqual([{
      type: "control",
      group: "agent",
      id: 7,
      action: 0
    }]);
  });

  it("serializes requests so BLE replies cannot cross sessions", async () => {
    const harness = processHarness();
    const opening = NativeBleProcessChannel.open(
      "C:\\Program Files\\Codex Micro\\native\\codex-ble-helper.exe",
      harness.spawn,
      "C:\\Program Files\\Codex Micro\\native"
    );
    await nextTurn();
    harness.process.stdout.write('{"id":1,"ok":true}\n');
    const transport = new CodexBleTransport(await opening);

    const first = transport.request({ method: "cfg.get_state" });
    const second = transport.request({
      method: "cfg.set_input",
      params: { joystick_sensitivity: 1.1 }
    });
    await nextTurn();
    expect(harness.commands.filter((entry) => entry.op === "write")).toHaveLength(1);
    harness.process.stdout.write('{"id":2,"ok":true}\n');

    harness.process.stdout.write(
      `${JSON.stringify({
        event: "notification",
        value_base64: Buffer.from('{"ok":true}\n').toString("base64")
      })}\n`
    );
    await first;
    await nextTurn();
    expect(harness.commands.filter((entry) => entry.op === "write")).toHaveLength(2);
    harness.process.stdout.write('{"id":3,"ok":true}\n');

    harness.process.stdout.write(
      `${JSON.stringify({
        event: "notification",
        value_base64: Buffer.from('{"ok":true}\n').toString("base64")
      })}\n`
    );
    await expect(second).resolves.toEqual({ ok: true });
  });

  it("rejects pending RPC when the native BLE link closes", async () => {
    const harness = processHarness();
    const opening = NativeBleProcessChannel.open(
      "C:\\Program Files\\Codex Micro\\native\\codex-ble-helper.exe",
      harness.spawn,
      "C:\\Program Files\\Codex Micro\\native"
    );
    await nextTurn();
    harness.process.stdout.write('{"id":1,"ok":true}\n');
    const transport = new CodexBleTransport(await opening);
    const pending = transport.request({ method: "cfg.get_state" });
    await nextTurn();

    harness.process.stdout.write('{"event":"closed","error":"link lost"}\n');

    await expect(pending).rejects.toThrow("BLE link closed: link lost");
    await transport.close();
    expect(harness.process.killed).toBe(true);
  });
});
