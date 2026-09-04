import { describe, expect, it } from "vitest";

import {
  ActionEngine,
  DeviceActionBridge,
  type WindowsInputAdapter
} from "./action-engine";
import type { AppControlEvent } from "../shared/protocol";


class EventSource {
  listener?: (event: AppControlEvent) => void;
  onEvent(listener: (event: AppControlEvent) => void): () => void {
    this.listener = listener;
    return () => { this.listener = undefined; };
  }
}

class StatusSource {
  listener?: (snapshot: { status: string }) => void;
  subscribe(listener: (snapshot: { status: string }) => void): () => void {
    this.listener = listener;
    return () => { this.listener = undefined; };
  }
}

class Input implements WindowsInputAdapter {
  readonly calls: string[] = [];
  async keyDown(key: string): Promise<void> {
    this.calls.push(`down:${key}`);
  }
  async keyUp(key: string): Promise<void> {
    this.calls.push(`up:${key}`);
  }
  async focusOrLaunch(target: string): Promise<void> {
    this.calls.push(`focus:${target}`);
  }
}

function command(action: 0 | 1): AppControlEvent {
  return { type: "control", group: "command", id: 6, action };
}

describe("DeviceActionBridge", () => {
  it("delivers input edges to the action engine in order", async () => {
    const events = new EventSource();
    const status = new StatusSource();
    const input = new Input();
    const engine = new ActionEngine(input, () => ({
      kind: "chord",
      keys: ["Control", "C"]
    }));
    const bridge = new DeviceActionBridge(events, status, engine);

    events.listener?.(command(1));
    events.listener?.(command(0));
    await bridge.flush();

    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "up:C",
      "up:Control"
    ]);
  });

  it("releases held keys when the App lease disconnects", async () => {
    const events = new EventSource();
    const status = new StatusSource();
    const input = new Input();
    const engine = new ActionEngine(input, () => ({
      kind: "chord",
      keys: ["Control", "C"]
    }));
    const bridge = new DeviceActionBridge(events, status, engine);

    events.listener?.(command(1));
    status.listener?.({ status: "disconnected" });
    await bridge.flush();

    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "up:C",
      "up:Control"
    ]);
    expect(engine.heldControlCount).toBe(0);
  });

  it("detaches listeners and releases keys when closed", async () => {
    const events = new EventSource();
    const status = new StatusSource();
    const input = new Input();
    const engine = new ActionEngine(input, () => ({
      kind: "chord",
      keys: ["Control", "C"]
    }));
    const bridge = new DeviceActionBridge(events, status, engine);

    events.listener?.(command(1));
    await bridge.close();

    expect(events.listener).toBeUndefined();
    expect(status.listener).toBeUndefined();
    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "up:C",
      "up:Control"
    ]);
  });
});
