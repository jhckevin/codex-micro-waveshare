import { describe, expect, it } from "vitest";

import {
  ActionEngine,
  windowsPresetAction,
  type WindowsAction,
  type WindowsInputAdapter
} from "./action-engine";
import type { AppControlEvent } from "../shared/protocol";


class RecordingInput implements WindowsInputAdapter {
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

function control(
  group: AppControlEvent["group"],
  id: number,
  action: AppControlEvent["action"],
  extra: Partial<AppControlEvent> = {}
): AppControlEvent {
  return { type: "control", group, id, action, ...extra };
}

function resolver(event: AppControlEvent): WindowsAction {
  if (event.group === "command" && event.id === 6) {
    return { kind: "chord", keys: ["Control", "C"] };
  }
  if (event.group === "command" && event.id === 7) {
    return { kind: "chord", keys: ["Control", "V"] };
  }
  if (event.group === "command" && event.id === 8) {
    return { kind: "focus-app", target: "codex" };
  }
  if (event.group === "command" && event.id === 9) {
    return { kind: "pulse", keys: ["RightAlt"] };
  }
  if (event.group === "encoder" && event.id === 18 && event.direction === 1) {
    return { kind: "chord", keys: ["Control", "ArrowUp"] };
  }
  return { kind: "unassigned" };
}

describe("ActionEngine", () => {
  it("holds a chord from press until release", async () => {
    const input = new RecordingInput();
    const engine = new ActionEngine(input, resolver);

    await engine.handle(control("command", 6, 1));
    await engine.handle(control("command", 6, 0));

    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "up:C",
      "up:Control"
    ]);
  });

  it("ignores duplicate presses and orphan releases", async () => {
    const input = new RecordingInput();
    const engine = new ActionEngine(input, resolver);

    await engine.handle(control("command", 6, 0));
    await engine.handle(control("command", 6, 1));
    await engine.handle(control("command", 6, 1));
    await engine.handle(control("command", 6, 0));
    await engine.handle(control("command", 6, 0));

    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "up:C",
      "up:Control"
    ]);
  });

  it("reference-counts modifiers shared by simultaneous controls", async () => {
    const input = new RecordingInput();
    const engine = new ActionEngine(input, resolver);

    await engine.handle(control("command", 6, 1));
    await engine.handle(control("command", 7, 1));
    await engine.handle(control("command", 6, 0));
    await engine.handle(control("command", 7, 0));

    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "down:V",
      "up:C",
      "up:V",
      "up:Control"
    ]);
  });

  it("releases every held key in reverse acquisition order on disconnect", async () => {
    const input = new RecordingInput();
    const engine = new ActionEngine(input, resolver);

    await engine.handle(control("command", 6, 1));
    await engine.handle(control("command", 7, 1));
    await engine.releaseAll();

    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "down:V",
      "up:V",
      "up:C",
      "up:Control"
    ]);
    expect(engine.heldControlCount).toBe(0);
  });

  it("focuses an application once per physical press", async () => {
    const input = new RecordingInput();
    const engine = new ActionEngine(input, resolver);

    await engine.handle(control("command", 8, 1));
    await engine.handle(control("command", 8, 1));
    await engine.handle(control("command", 8, 0));
    await engine.handle(control("command", 8, 1));

    expect(input.calls).toEqual(["focus:codex", "focus:codex"]);
  });

  it("pulses a toggle shortcut once on physical press", async () => {
    const input = new RecordingInput();
    const engine = new ActionEngine(input, resolver);

    await engine.handle(control("command", 9, 1));
    await engine.handle(control("command", 9, 1));
    await engine.handle(control("command", 9, 0));
    await engine.handle(control("command", 9, 1));
    await engine.handle(control("command", 9, 0));

    expect(input.calls).toEqual([
      "down:RightAlt",
      "up:RightAlt",
      "down:RightAlt",
      "up:RightAlt"
    ]);
    expect(engine.heldControlCount).toBe(0);
  });

  it("executes encoder rotation as an immediate pulse", async () => {
    const input = new RecordingInput();
    const engine = new ActionEngine(input, resolver);

    await engine.handle(
      control("encoder", 18, 2, { direction: 1 })
    );

    expect(input.calls).toEqual([
      "down:Control",
      "down:ArrowUp",
      "up:ArrowUp",
      "up:Control"
    ]);
    expect(engine.heldControlCount).toBe(0);
  });

  it("rolls back keys acquired before a key-down failure", async () => {
    const input = new RecordingInput();
    input.keyDown = async (key: string) => {
      input.calls.push(`down:${key}`);
      if (key === "C") throw new Error("injection failed");
    };
    const engine = new ActionEngine(input, resolver);

    await expect(engine.handle(control("command", 6, 1))).rejects.toThrow(
      "injection failed"
    );

    expect(input.calls).toEqual([
      "down:Control",
      "down:C",
      "up:Control"
    ]);
    expect(engine.heldControlCount).toBe(0);
  });
});

describe("windowsPresetAction", () => {
  it.each([
    ["copy", { kind: "chord", keys: ["Control", "C"] }],
    ["paste", { kind: "chord", keys: ["Control", "V"] }],
    ["cut", { kind: "chord", keys: ["Control", "X"] }],
    ["windows-key", { kind: "chord", keys: ["Windows"] }],
    ["click-to-do", { kind: "chord", keys: ["Windows", "Q"] }],
    ["codex", { kind: "focus-app", target: "codex" }],
    ["chatgpt", { kind: "focus-app", target: "chatgpt" }],
    ["always-on-top", { kind: "chord", keys: ["Windows", "Control", "T"] }],
    ["typeless", { kind: "chord", keys: ["Alt", "Space"] }],
    ["typeless-hold", { kind: "chord", keys: ["RightAlt"] }],
    ["typeless-press", { kind: "pulse", keys: ["RightAlt"] }],
    ["voice-typing", { kind: "chord", keys: ["Windows", "H"] }],
    ["command-palette", { kind: "chord", keys: ["Windows", "Alt", "Space"] }]
  ] as const)("maps %s to its documented Windows action", (preset, expected) => {
    expect(windowsPresetAction(preset)).toEqual(expected);
  });

  it("fails closed for an unknown preset", () => {
    expect(windowsPresetAction("unknown" as never)).toEqual({
      kind: "unassigned"
    });
  });
});
