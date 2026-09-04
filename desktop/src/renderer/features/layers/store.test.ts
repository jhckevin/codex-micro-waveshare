import { describe, expect, it } from "vitest";

import { createDefaultProfile } from "./model";
import { layerEditorReducer } from "./store";

describe("layer editor store", () => {
  it("replaces the editor profile with the durable main-process profile", () => {
    const state = {
      profile: createDefaultProfile(),
      selectedLayer: 4,
      selectedKey: { group: "command" as const, index: 4 }
    };
    const profile = createDefaultProfile({ layerCount: 3 });

    const next = layerEditorReducer(state, {
      type: "replace-profile",
      profile
    });

    expect(next.profile).toEqual(profile);
    expect(next.selectedLayer).toBe(1);
    expect(next.selectedKey).toBeNull();
  });

  it("selects layers and clamps the active layer count", () => {
    let state = {
      profile: createDefaultProfile(),
      selectedLayer: 6,
      selectedKey: null
    };
    state = layerEditorReducer(state, { type: "set-layer-count", count: 2 });
    expect(state.profile.activeLayerCount).toBe(2);
    expect(state.selectedLayer).toBe(2);
  });

  it("does not mutate prior snapshots", () => {
    const initial = {
      profile: createDefaultProfile(),
      selectedLayer: 2,
      selectedKey: null
    };
    const next = layerEditorReducer(initial, {
      type: "set-icon",
      layer: 2,
      group: "command",
      index: 0,
      icon: { kind: "blank" }
    });
    expect(initial.profile.layers[1].keys.command[0].icon.kind).toBe("inherit");
    expect(next.profile.layers[1].keys.command[0].icon.kind).toBe("blank");
  });
});
