import { describe, expect, it } from "vitest";

import {
  createDefaultProfile,
  flatControlId,
  setKeyAction,
  setKeyIcon,
  setLayerCodexPassthrough,
  toRoutingConfig
} from "./model";

describe("layer editor model", () => {
  it("starts with Codex Classical on Layer 1 and inherited icons above it", () => {
    const profile = createDefaultProfile();
    expect(profile.layers[0].name).toBe("Codex Classical");
    expect(profile.layers[0].keys.agent[0].action).toEqual({ kind: "codex" });
    expect(profile.layers[1].keys.agent[0].icon).toEqual({
      kind: "inherit",
      layer: 1,
      group: "agent",
      index: 0
    });
  });

  it("keeps icon and action edits independent", () => {
    const initial = createDefaultProfile();
    const withIcon = setKeyIcon(initial, 2, "command", 1, {
      kind: "builtin",
      id: "copy"
    });
    const withAction = setKeyAction(withIcon, 2, "command", 1, {
      kind: "shortcut",
      preset: "copy"
    });

    expect(withAction.layers[1].keys.command[1].icon).toEqual({
      kind: "builtin",
      id: "copy"
    });
    expect(withAction.layers[1].keys.command[1].action).toEqual({
      kind: "shortcut",
      preset: "copy"
    });
  });

  it("protects Layer 1 Agent actions but permits their icons to change", () => {
    const initial = createDefaultProfile();
    expect(() =>
      setKeyAction(initial, 1, "agent", 0, {
        kind: "shortcut",
        preset: "copy"
      })
    ).toThrow(/Layer 1 Agent/i);

    expect(
      setKeyIcon(initial, 1, "agent", 0, { kind: "blank" })
        .layers[0].keys.agent[0].icon
    ).toEqual({ kind: "blank" });
  });

  it("calculates private identifiers locally without a layer field", () => {
    expect(flatControlId(1, 0)).toBe(0);
    expect(flatControlId(2, 0)).toBe(6);
    expect(flatControlId(6, 5)).toBe(35);
  });

  it("supports explicit higher-layer Codex passthrough", () => {
    const profile = setLayerCodexPassthrough(
      createDefaultProfile(),
      3,
      "joystick",
      0,
      true
    );
    expect(profile.layers[2].keys.joystick[0].action).toEqual({ kind: "codex" });
  });

  it("limits cycling to two through six layers", () => {
    const profile = createDefaultProfile({ layerCount: 3 });
    expect(profile.activeLayerCount).toBe(3);
    expect(() => createDefaultProfile({ layerCount: 1 })).toThrow();
    expect(() => createDefaultProfile({ layerCount: 7 })).toThrow();
  });

  it("serializes routes without exposing the selected layer in input events", () => {
    let profile = createDefaultProfile({ layerCount: 3 });
    profile = setKeyAction(profile, 1, "command", 0, {
      kind: "shortcut",
      preset: "copy"
    });
    profile = setKeyAction(profile, 2, "agent", 0, {
      kind: "shortcut",
      preset: "copy"
    });
    profile = setLayerCodexPassthrough(profile, 2, "agent", 1, true);
    const routing = toRoutingConfig(profile);
    expect(routing.layer_count).toBe(3);
    expect(routing.layer1_command_targets[0]).toBe(7);
    expect(routing.higher_codex_masks[0][0]).toBe(0b111110);
    expect(JSON.stringify(routing)).not.toContain("\"layer\":");
  });
});
