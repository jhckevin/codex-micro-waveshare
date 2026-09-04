import { describe, expect, it } from "vitest";

import { windowsPresetAction } from "../../../main/action-engine";
import { decodeAppControlEvent } from "../../../shared/protocol";
import {
  createDefaultProfile,
  setKeyAction,
  setLayerCodexPassthrough,
  toRoutingConfig
} from "./model";

describe("Layer 1 shadow routing", () => {
  it("routes a replaced Layer 1 MIC to ids outside all six physical layers", () => {
    const profile = setKeyAction(
      createDefaultProfile(),
      1,
      "command",
      4,
      { kind: "shortcut", preset: "typeless-hold" }
    );

    const config = toRoutingConfig(profile);

    expect(config.layer1_command_targets).toEqual([0, 0, 0, 0, 7, 0]);
    expect(decodeAppControlEvent({
      event: "input",
      group: "command",
      id: 40,
      action: 1
    })).toEqual({
      type: "control",
      group: "command",
      id: 40,
      action: 1
    });
    expect(() => decodeAppControlEvent({
      event: "input",
      group: "command",
      id: 42,
      action: 1
    })).toThrow("Malformed private control event");
  });

  it("keeps higher-layer Codex passthrough mapped to the original Codex layer", () => {
    const profile = setLayerCodexPassthrough(
      createDefaultProfile(),
      3,
      "command",
      4,
      true
    );

    const config = toRoutingConfig(profile);

    expect(config.higher_codex_masks[1][1] & (1 << 4)).toBe(1 << 4);
    expect(config.layer1_command_targets[4]).toBe(0);
  });

  it("models Typeless hold as a real right-Alt down/up chord", () => {
    expect(windowsPresetAction("typeless-hold")).toEqual({
      kind: "chord",
      keys: ["RightAlt"]
    });
  });

  it("models Typeless toggle as a one-shot right-Alt pulse", () => {
    expect(windowsPresetAction("typeless-press")).toEqual({
      kind: "pulse",
      keys: ["RightAlt"]
    });
  });
});
