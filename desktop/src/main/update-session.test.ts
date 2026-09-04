import { describe, expect, it } from "vitest";

import type { DeviceTransport } from "./device-session";
import { DeviceUpdateRelay, validateCiphertextOnlyPackage } from "./update-session";

class FakeTransport implements DeviceTransport {
  readonly kind = "usb" as const;
  calls: Array<{ method: string; params?: Record<string, unknown> }> = [];
  async request(message: { method: string; params?: Record<string, unknown> }) {
    this.calls.push(message);
    if (message.method === "update.attest") {
      return {
        ok: true,
        device_id: "11".repeat(16),
        tag: "22".repeat(32)
      };
    }
    if (message.method === "update.begin") {
      return { ok: true, state: "receiving", expected_offset: 0, next_sequence: 0, total_size: 2 };
    }
    if (message.method === "update.status") {
      return { ok: true, state: "idle", expected_offset: 0, next_sequence: 0, total_size: 0 };
    }
    if (message.method === "update.chunk") {
      return { ok: true, state: "receiving", expected_offset: 2, next_sequence: 1, total_size: 2 };
    }
    return { ok: true, state: "complete", expected_offset: 2, next_sequence: 1, total_size: 2 };
  }
  close() {}
}

describe("DeviceUpdateRelay", () => {
  it("relays a server challenge to the hardware identity", async () => {
    const transport = new FakeTransport();
    const relay = new DeviceUpdateRelay(transport);
    await expect(relay.attest("aa".repeat(32))).resolves.toEqual({
      device_id: "11".repeat(16),
      tag: "22".repeat(32)
    });
    expect(transport.calls[0]).toEqual({
      method: "update.attest",
      params: { challenge: "aa".repeat(32) }
    });
  });

  it("relays ciphertext and authentication tags without plaintext", async () => {
    const transport = new FakeTransport();
    const relay = new DeviceUpdateRelay(transport);
    await relay.install({
      target_version: "2.0.0",
      estimated_seconds: 10,
      package_class: "compatibility",
      manifest_hex: "00".repeat(248),
      chunks: [{
        offset: 0,
        sequence: 0,
        ciphertext_hex: "aabb",
        tag_hex: "11".repeat(16),
        plaintext_sha256_hex: "22".repeat(32)
      }]
    });
    expect(transport.calls.map((call) => call.method)).toEqual([
      "update.status", "update.begin", "update.chunk", "update.commit"
    ]);
    expect(JSON.stringify(transport.calls)).not.toContain("plaintext");
  });

  it("relays signed user content through the same USB-only channel", async () => {
    const transport = new FakeTransport();
    const relay = new DeviceUpdateRelay(transport);
    await relay.install({
      target_version: "icons.1",
      estimated_seconds: 3,
      package_class: "user_content",
      manifest_hex: "00".repeat(248),
      chunks: [{
        offset: 0,
        sequence: 0,
        ciphertext_hex: "aabb",
        tag_hex: "11".repeat(16),
        plaintext_sha256_hex: "22".repeat(32)
      }]
    });

    expect(transport.calls.map((call) => call.method)).toEqual([
      "update.status", "update.begin", "update.chunk", "update.commit"
    ]);
  });

  it("resumes a matching in-progress package without restarting begin", async () => {
    const transport = new FakeTransport();
    transport.request = async (message) => {
      transport.calls.push(message);
      if (message.method === "update.status") {
        return {ok: true, state: "receiving", expected_offset: 2,
          next_sequence: 1, total_size: 4, target_version: "2.0.0"};
      }
      if (message.method === "update.chunk") {
        return {ok: true, state: "receiving", expected_offset: 4,
          next_sequence: 2, total_size: 4, target_version: "2.0.0"};
      }
      return {ok: true, state: "complete", expected_offset: 4,
        next_sequence: 2, total_size: 4, target_version: "2.0.0"};
    };
    const relay = new DeviceUpdateRelay(transport);
    await relay.install({
      target_version: "2.0.0", estimated_seconds: 10,
      package_class: "complete_firmware", manifest_hex: "00".repeat(248),
      chunks: [0, 2].map((offset, sequence) => ({
        offset, sequence, ciphertext_hex: "aabb",
        tag_hex: "11".repeat(16), plaintext_sha256_hex: "22".repeat(32)
      }))
    });
    expect(transport.calls.map((call) => call.method)).toEqual([
      "update.status", "update.chunk", "update.commit"
    ]);
  });

  it("rejects packages containing malformed encrypted fields", () => {
    expect(() => validateCiphertextOnlyPackage({
      target_version: "x",
      estimated_seconds: 1,
      package_class: "complete_firmware",
      manifest_hex: "00".repeat(248),
      chunks: [{
        offset: 0, sequence: 0, ciphertext_hex: "not-hex",
        tag_hex: "00".repeat(16), plaintext_sha256_hex: "00".repeat(32)
      }]
    })).toThrow();
  });
});
