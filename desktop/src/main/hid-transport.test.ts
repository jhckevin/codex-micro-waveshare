import { EventEmitter } from "node:events";
import { describe, expect, it } from "vitest";

import { CodexHidTransport, type AsyncHidDevice } from "./hid-transport";

class FakeHid extends EventEmitter implements AsyncHidDevice {
  writes: number[][] = [];
  async write(data: number[]): Promise<number> {
    this.writes.push(data);
    return data.length;
  }
  async close(): Promise<void> {}
}

describe("CodexHidTransport", () => {
  it("frames report 7 and reassembles a JSON reply", async () => {
    const device = new FakeHid();
    const transport = new CodexHidTransport(device);
    const pending = transport.request({ method: "update.hello" });
    await Promise.resolve();
    expect(device.writes[0]).toHaveLength(64);
    expect(device.writes[0]?.[0]).toBe(7);
    expect(device.writes[0]?.[1]).toBe(2);
    const reply = Buffer.from('{"ok":true}\n');
    device.emit("data", Buffer.from([7, 2, 5, ...reply.subarray(0, 5)]));
    device.emit("data", Buffer.from([7, 2, reply.length - 5, ...reply.subarray(5)]));
    await expect(pending).resolves.toEqual({ ok: true });
  });

  it("delivers unsolicited input without resolving an in-flight RPC", async () => {
    const device = new FakeHid();
    const transport = new CodexHidTransport(device);
    const events: unknown[] = [];
    transport.onEvent((event) => events.push(event));
    const pending = transport.request({ method: "routing.get" });
    await Promise.resolve();

    const input = Buffer.from(
      '{"event":"input","group":"command","id":6,"action":1}\n'
    );
    device.emit("data", Buffer.from([7, 2, input.length, ...input]));
    await Promise.resolve();

    expect(events).toEqual([
      {
        type: "control",
        group: "command",
        id: 6,
        action: 1
      }
    ]);
    const reply = Buffer.from('{"ok":true,"enabled":true}\n');
    device.emit("data", Buffer.from([7, 2, reply.length, ...reply]));
    await expect(pending).resolves.toEqual({ ok: true, enabled: true });
  });

  it("preserves unsolicited press and release order", () => {
    const device = new FakeHid();
    const transport = new CodexHidTransport(device);
    const actions: number[] = [];
    const unsubscribe = transport.onEvent((event) => actions.push(event.action));
    const press = Buffer.from(
      '{"event":"input","group":"agent","id":12,"action":1}\n'
    );
    const release = Buffer.from(
      '{"event":"input","group":"agent","id":12,"action":0}\n'
    );

    device.emit("data", Buffer.from([7, 2, press.length, ...press]));
    device.emit("data", Buffer.from([7, 2, release.length, ...release]));
    unsubscribe();
    device.emit("data", Buffer.from([7, 2, press.length, ...press]));

    expect(actions).toEqual([1, 0]);
  });
});
