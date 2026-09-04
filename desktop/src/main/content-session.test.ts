import { describe, expect, it } from "vitest";

import type { DeviceTransport } from "./device-session";
import { DeviceContentSession } from "./content-session";

function pack(size = 600, value = 0x5a): Uint8Array {
  const result = new Uint8Array(size).fill(value);
  result.set(new TextEncoder().encode("CMC1"), 0);
  result[4] = 1;
  result[6] = 48;
  result[7] = 48;
  new DataView(result.buffer).setUint32(8, size, true);
  return result;
}

class FakeContentTransport implements DeviceTransport {
  readonly kind = "usb" as const;
  readonly calls: Array<{
    method: string;
    params?: Record<string, unknown>;
  }> = [];

  constructor(private readonly resumeOffset = 0) {}

  async request(message: {
    method: string;
    params?: Record<string, unknown>;
  }) {
    this.calls.push(message);
    if (message.method === "content.begin") {
      return {
        ok: true,
        state: "receiving",
        expected_offset: this.resumeOffset,
        total_size: 600
      };
    }
    if (message.method === "content.chunk") {
      const offset = Number(message.params?.offset ?? 0);
      const bytes = String(message.params?.data ?? "").length / 2;
      return {
        ok: true,
        state: "receiving",
        expected_offset: offset + bytes,
        total_size: 600
      };
    }
    return {
      ok: true,
      state: "complete",
      expected_offset: 600,
      total_size: 600
    };
  }

  close() {}
}

describe("DeviceContentSession", () => {
  it("installs a device-ready pack in bounded USB chunks", async () => {
    const transport = new FakeContentTransport();
    const session = new DeviceContentSession(transport);
    const content = pack();

    await expect(session.install(content)).resolves.toMatchObject({
      state: "complete",
      expected_offset: 600
    });
    expect(transport.calls.map((call) => call.method)).toEqual([
      "content.begin",
      "content.chunk",
      "content.chunk",
      "content.commit"
    ]);
    expect(transport.calls[1]?.params).toMatchObject({ offset: 0 });
    expect(transport.calls[2]?.params).toMatchObject({ offset: 512 });
    expect(String(transport.calls[1]?.params?.data)).toHaveLength(1024);
  });

  it("resumes an interrupted transfer only from the device offset", async () => {
    const transport = new FakeContentTransport(512);
    const session = new DeviceContentSession(transport);

    await session.install(pack(600, 0x44));

    expect(transport.calls.map((call) => call.method)).toEqual([
      "content.begin",
      "content.chunk",
      "content.commit"
    ]);
    expect(transport.calls[1]?.params).toMatchObject({ offset: 512 });
  });

  it("rejects BLE before transmitting user content", async () => {
    const transport: DeviceTransport = {
      kind: "ble",
      request: async () => ({ ok: true }),
      close() {}
    };
    const session = new DeviceContentSession(transport);

    await expect(session.install(pack(16))).rejects.toThrow(/USB/);
  });
});
