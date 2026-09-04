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

class Detector implements WindowsInputAdapter {
  readonly down = new Set<string>();
  presses = 0;
  releases = 0;
  async keyDown(key: string): Promise<void> {
    this.down.add(key);
    this.presses += 1;
  }
  async keyUp(key: string): Promise<void> {
    this.down.delete(key);
    this.releases += 1;
  }
  async focusOrLaunch(): Promise<void> {}
}

function edge(id: number, action: 0 | 1): AppControlEvent {
  return { type: "control", group: "command", id, action };
}

describe("production input event simulation", () => {
  it("does not lose releases during a dense six-control event stream", async () => {
    const events = new EventSource();
    const status = new StatusSource();
    const detector = new Detector();
    const engine = new ActionEngine(detector, (event) => ({
      kind: "chord",
      keys: ["Control", `Key${event.id}`]
    }));
    const bridge = new DeviceActionBridge(events, status, engine);

    for (let cycle = 0; cycle < 250; cycle += 1) {
      for (let id = 6; id < 12; id += 1) events.listener?.(edge(id, 1));
      for (let id = 11; id >= 6; id -= 1) events.listener?.(edge(id, 0));
    }
    await bridge.flush();

    expect(detector.down.size).toBe(0);
    expect(engine.heldControlCount).toBe(0);
    expect(detector.presses).toBe(detector.releases);
    expect(detector.presses).toBe(1_750);
  });

  it("releases every injected key when the device disconnects mid-storm", async () => {
    const events = new EventSource();
    const status = new StatusSource();
    const detector = new Detector();
    const engine = new ActionEngine(detector, () => ({
      kind: "chord",
      keys: ["RightAlt"]
    }));
    const bridge = new DeviceActionBridge(events, status, engine);
    for (let id = 6; id < 12; id += 1) events.listener?.(edge(id, 1));
    status.listener?.({ status: "disconnected" });
    await bridge.flush();
    expect(detector.down.size).toBe(0);
    expect(engine.heldControlCount).toBe(0);
  });
});
