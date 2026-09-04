import { describe, expect, it } from "vitest";

import {
  ProfileRuntime,
  createDefaultRuntimeProfile,
  isRuntimeDeviceProfile,
  toRuntimeRoutingConfig
} from "./profile-runtime";

describe("ProfileRuntime", () => {
  it("resolves the Layer 1 MIC shadow id to the configured hold action", () => {
    const profile = createDefaultRuntimeProfile();
    profile.layers[0].keys.command[4].action = {
      kind: "shortcut",
      preset: "typeless-hold"
    };
    const runtime = new ProfileRuntime(profile);

    expect(runtime.resolve({
      type: "control",
      group: "command",
      id: 40,
      action: 1
    })).toEqual({ kind: "chord", keys: ["RightAlt"] });
  });

  it("resolves the Layer 1 MIC shadow id to a right-Alt press pulse", () => {
    const profile = createDefaultRuntimeProfile();
    profile.layers[0].keys.command[4].action = {
      kind: "shortcut",
      preset: "typeless-press"
    };
    const runtime = new ProfileRuntime(profile);

    expect(runtime.resolve({
      type: "control",
      group: "command",
      id: 40,
      action: 1
    })).toEqual({
      kind: "pulse",
      keys: ["RightAlt"]
    });
  });

  it("resolves custom chords and applications on higher flat ids", () => {
    const profile = createDefaultRuntimeProfile();
    profile.layers[2].keys.command[4].action = {
      kind: "custom-chord",
      keys: ["Control", "Shift", "K"]
    };
    profile.layers[1].keys.command[4].action = {
      kind: "custom-pulse",
      keys: ["RightAlt"]
    };
    profile.layers[3].keys.command[1].action = {
      kind: "launch-app",
      target: "C:\\Program Files\\Example\\Example.exe"
    };
    const runtime = new ProfileRuntime(profile);

    expect(runtime.resolve({
      type: "control",
      group: "command",
      id: 16,
      action: 1
    })).toEqual({
      kind: "chord",
      keys: ["Control", "Shift", "K"]
    });
    expect(runtime.resolve({
      type: "control",
      group: "command",
      id: 10,
      action: 1
    })).toEqual({
      kind: "pulse",
      keys: ["RightAlt"]
    });
    expect(runtime.resolve({
      type: "control",
      group: "command",
      id: 19,
      action: 1
    })).toEqual({
      kind: "focus-app",
      target: "C:\\Program Files\\Example\\Example.exe"
    });
  });

  it("keeps every physical MIC independently addressable without exposing layers", () => {
    const profile = createDefaultRuntimeProfile();
    const micIds = [40, 10, 16, 22, 28, 34];
    for (let layerIndex = 0; layerIndex < 6; layerIndex += 1) {
      profile.layers[layerIndex].keys.command[4].action = {
        kind: "custom-pulse",
        keys: [`F${layerIndex + 1}`]
      };
    }
    const runtime = new ProfileRuntime(profile);
    const routing = toRuntimeRoutingConfig(profile);

    expect(routing.layer1_command_targets[4]).toBe(7);
    for (let layerIndex = 1; layerIndex < 6; layerIndex += 1) {
      expect(routing.higher_codex_masks[layerIndex - 1][1] & (1 << 4)).toBe(0);
    }
    micIds.forEach((id, layerIndex) => {
      expect(runtime.resolve({
        type: "control",
        group: "command",
        id,
        action: 1
      })).toEqual({
        kind: "pulse",
        keys: [`F${layerIndex + 1}`]
      });
    });
  });

  it("does not execute Codex or unassigned routes in the private channel", () => {
    const runtime = new ProfileRuntime(createDefaultRuntimeProfile());
    expect(runtime.resolve({
      type: "control",
      group: "command",
      id: 0,
      action: 1
    })).toEqual({ kind: "unassigned" });
    expect(runtime.resolve({
      type: "control",
      group: "command",
      id: 6,
      action: 1
    })).toEqual({ kind: "unassigned" });
  });

  it("keeps every unconfigured higher-layer control on Codex", () => {
    const profile = createDefaultRuntimeProfile();
    const defaults = toRuntimeRoutingConfig(profile);
    expect(defaults.higher_codex_masks[0]).toEqual([63, 63, 1, 1]);

    profile.layers[2].keys.command[4].action = {
      kind: "shortcut",
      preset: "typeless-hold"
    };
    profile.layers[0].keys.command[4].action = {
      kind: "shortcut",
      preset: "typeless-hold"
    };
    const configured = toRuntimeRoutingConfig(profile);
    expect(configured.layer1_command_targets[4]).toBe(7);
    expect(configured.higher_codex_masks[1][1] & (1 << 4)).toBe(0);
  });

  it("rejects malformed profile objects before they reach action injection", () => {
    const profile = createDefaultRuntimeProfile() as unknown as {
      layers: Array<{ keys: { command: Array<{ action: unknown }> } }>;
    };
    profile.layers[1].keys.command[0].action = {
      kind: "custom-chord",
      keys: ["Control", "A"],
      injected: true
    };

    expect(isRuntimeDeviceProfile(profile)).toBe(false);
    expect(() => new ProfileRuntime(profile)).toThrow(
      "profile validation failed"
    );
  });
});
