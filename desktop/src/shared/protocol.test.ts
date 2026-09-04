import { describe, expect, it } from "vitest";

import {
  decodeAppControlEvent,
  decodeDeviceReply,
  isUsbUpdateTransport,
  type DeviceTransportKind
} from "./protocol";

describe("device protocol", () => {
  it("decodes a successful protocol reply", () => {
    expect(
      decodeDeviceReply({
        ok: true,
        method: "app.hello",
        protocol: 1,
        heartbeat_ms: 500,
        lease_ms: 1500
      })
    ).toMatchObject({
      ok: true,
      method: "app.hello",
      protocol: 1
    });
  });

  it.each([
    null,
    [],
    { ok: "yes", method: "app.hello" },
    { ok: true, method: 1 },
    { ok: true, method: "app.hello", protocol: 2 }
  ])("rejects malformed or unsupported replies: %j", (value) => {
    expect(() => decodeDeviceReply(value)).toThrow();
  });

  it.each([
    [
      { event: "input", group: "command", id: 6, action: 1 },
      { type: "control", group: "command", id: 6, action: 1 }
    ],
    [
      { event: "input", group: "encoder", id: 18, action: 2, direction: -1 },
      {
        type: "control",
        group: "encoder",
        id: 18,
        action: 2,
        direction: -1
      }
    ],
    [
      {
        event: "input",
        group: "joystick",
        id: 24,
        action: 3,
        angle_milli: 250,
        distance_milli: 800
      },
      {
        type: "control",
        group: "joystick",
        id: 24,
        action: 3,
        angle_milli: 250,
        distance_milli: 800
      }
    ]
  ])("decodes firmware private input %j", (wire, expected) => {
    expect(decodeAppControlEvent(wire)).toEqual(expected);
  });

  it.each([
    { event: "input", group: "command", id: 6, action: 1, layer: 2 },
    { event: "input", group: "command", id: 42, action: 1 },
    { event: "input", group: "encoder", id: 18, action: 2 },
    { event: "input", group: "joystick", id: 24, action: 3, angle_milli: 1001, distance_milli: 1 }
  ])("rejects malformed firmware private input %j", (wire) => {
    expect(() => decodeAppControlEvent(wire)).toThrow();
  });

  it("allows firmware updates over USB only", () => {
    const kinds: DeviceTransportKind[] = ["usb", "ble"];
    expect(kinds.map(isUsbUpdateTransport)).toEqual([true, false]);
  });
});
