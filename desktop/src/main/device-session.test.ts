import { describe, expect, it, vi } from "vitest";

import {
  DeviceSession,
  type DeviceTransport,
  type IntervalScheduler
} from "./device-session";

function makeTransport(): DeviceTransport & {
  sent: Array<Record<string, unknown>>;
} {
  const sent: Array<Record<string, unknown>> = [];
  return {
    kind: "usb",
    sent,
    async request(message) {
      sent.push(message);
      return {
        ok: true,
        method: String(message.method),
        protocol: 1,
        heartbeat_ms: 500,
        lease_ms: 1500
      };
    },
    close: vi.fn()
  };
}

describe("DeviceSession", () => {
  it("negotiates protocol 1 and sends a heartbeat every 500 ms", async () => {
    const transport = makeTransport();
    let callback: (() => void) | undefined;
    let intervalMs = 0;
    const scheduler: IntervalScheduler = {
      setInterval(fn, ms) {
        callback = fn;
        intervalMs = ms;
        return 17;
      },
      clearInterval: vi.fn()
    };
    const session = new DeviceSession(transport, scheduler);

    await session.connect();
    expect(transport.sent[0]).toEqual({
      method: "app.hello",
      params: { protocol: 1 }
    });
    expect(intervalMs).toBe(500);

    callback?.();
    await Promise.resolve();
    expect(transport.sent[1]).toEqual({
      method: "app.heartbeat",
      params: { protocol: 1 }
    });
  });

  it("notifies listeners and releases the lease on disconnect", async () => {
    const transport = makeTransport();
    const states: string[] = [];
    const scheduler: IntervalScheduler = {
      setInterval: () => 9,
      clearInterval: vi.fn()
    };
    const session = new DeviceSession(transport, scheduler);
    session.subscribe((snapshot) => states.push(snapshot.status));

    await session.connect();
    await session.disconnect();

    expect(transport.sent.at(-1)).toEqual({
      method: "app.goodbye",
      params: { protocol: 1 }
    });
    expect(states).toEqual(["connecting", "connected", "disconnecting", "disconnected"]);
    expect(transport.close).toHaveBeenCalledOnce();
  });

  it("fails closed when hello cannot be decoded", async () => {
    const transport = makeTransport();
    transport.request = async () => ({ ok: true, method: "app.hello", protocol: 99 });
    const session = new DeviceSession(transport);

    await expect(session.connect()).rejects.toThrow();
    expect(session.snapshot.status).toBe("disconnected");
    expect(transport.close).toHaveBeenCalledOnce();
  });
});
