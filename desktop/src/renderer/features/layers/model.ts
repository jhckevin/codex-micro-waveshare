import type { ControlGroup, RoutingConfig } from "../../../shared/protocol";

export type EditableControlGroup = ControlGroup;
export type ShortcutPresetId =
  | "copy"
  | "paste"
  | "cut"
  | "windows-key"
  | "click-to-do"
  | "codex"
  | "chatgpt"
  | "always-on-top"
  | "typeless"
  | "typeless-hold"
  | "typeless-press"
  | "voice-typing"
  | "command-palette";

export type IconRef =
  | { kind: "builtin"; id: string }
  | { kind: "custom"; id: string }
  | { kind: "inherit"; layer: number; group: EditableControlGroup; index: number }
  | { kind: "blank" };

export type KeyAction =
  | { kind: "codex" }
  | { kind: "unassigned" }
  | { kind: "shortcut"; preset: ShortcutPresetId }
  | { kind: "custom-chord"; keys: readonly string[] }
  | { kind: "custom-pulse"; keys: readonly string[] }
  | { kind: "launch-app"; target: string };

export interface KeyDefinition {
  icon: IconRef;
  action: KeyAction;
}

export type LayerKeys = Record<EditableControlGroup, readonly KeyDefinition[]>;

export interface LayerDefinition {
  number: number;
  name: string;
  keys: LayerKeys;
}

export interface DeviceProfile {
  activeLayerCount: number;
  layers: readonly LayerDefinition[];
}

export interface CreateProfileOptions {
  layerCount?: number;
}

export const PHYSICAL_CONTROL_COUNTS: Readonly<Record<EditableControlGroup, number>> = {
  agent: 6,
  command: 6,
  encoder: 1,
  joystick: 1
};

const defaultCommandIcons = ["bolt", "check", "close", "computer-use", "microphone", "codex"];

function assertLayer(layer: number): void {
  if (!Number.isInteger(layer) || layer < 1 || layer > 6) {
    throw new RangeError("Layer must be between 1 and 6");
  }
}

function assertKey(group: EditableControlGroup, index: number): void {
  if (!Number.isInteger(index) || index < 0 || index >= PHYSICAL_CONTROL_COUNTS[group]) {
    throw new RangeError(`Invalid ${group} key index`);
  }
}

function defaultIcon(layer: number, group: EditableControlGroup, index: number): IconRef {
  if (layer > 1) return { kind: "inherit", layer: 1, group, index };
  if (group === "command") return { kind: "builtin", id: defaultCommandIcons[index] };
  if (group === "agent") return { kind: "builtin", id: `agent-${index + 1}` };
  return { kind: "builtin", id: group };
}

function makeKeys(layer: number): LayerKeys {
  const makeGroup = (group: EditableControlGroup): readonly KeyDefinition[] =>
    Array.from({ length: PHYSICAL_CONTROL_COUNTS[group] }, (_, index) => ({
      icon: defaultIcon(layer, group, index),
      action: { kind: "codex" as const }
    }));
  return {
    agent: makeGroup("agent"),
    command: makeGroup("command"),
    encoder: makeGroup("encoder"),
    joystick: makeGroup("joystick")
  };
}

export function createDefaultProfile(options: CreateProfileOptions = {}): DeviceProfile {
  const activeLayerCount = options.layerCount ?? 6;
  if (!Number.isInteger(activeLayerCount) || activeLayerCount < 2 || activeLayerCount > 6) {
    throw new RangeError("Active layer count must be between 2 and 6");
  }
  return {
    activeLayerCount,
    layers: Array.from({ length: 6 }, (_, index) => ({
      number: index + 1,
      name: index === 0 ? "Codex Classical" : `Layer ${index + 1}`,
      keys: makeKeys(index + 1)
    }))
  };
}

export function flatControlId(layer: number, physicalIndex: number): number {
  assertLayer(layer);
  if (!Number.isInteger(physicalIndex) || physicalIndex < 0 || physicalIndex > 5) {
    throw new RangeError("Physical control index must be between 0 and 5");
  }
  return (layer - 1) * 6 + physicalIndex;
}

function updateKey(
  profile: DeviceProfile,
  layer: number,
  group: EditableControlGroup,
  index: number,
  updater: (key: KeyDefinition) => KeyDefinition
): DeviceProfile {
  assertLayer(layer);
  assertKey(group, index);
  const layerIndex = layer - 1;
  const currentLayer = profile.layers[layerIndex];
  const keys = [...currentLayer.keys[group]];
  keys[index] = updater(keys[index]);
  const layers = [...profile.layers];
  layers[layerIndex] = {
    ...currentLayer,
    keys: { ...currentLayer.keys, [group]: keys }
  };
  return { ...profile, layers };
}

export function setKeyIcon(
  profile: DeviceProfile,
  layer: number,
  group: EditableControlGroup,
  index: number,
  icon: IconRef
): DeviceProfile {
  return updateKey(profile, layer, group, index, (key) => ({ ...key, icon }));
}

export function setKeyAction(
  profile: DeviceProfile,
  layer: number,
  group: EditableControlGroup,
  index: number,
  action: KeyAction
): DeviceProfile {
  if (layer === 1 && group === "agent" && action.kind !== "codex") {
    throw new Error("Layer 1 Agent actions are reserved for Codex");
  }
  return updateKey(profile, layer, group, index, (key) => ({ ...key, action }));
}

export function setLayerCodexPassthrough(
  profile: DeviceProfile,
  layer: number,
  group: EditableControlGroup,
  index: number,
  enabled: boolean
): DeviceProfile {
  if (layer === 1) throw new Error("Layer 1 already uses Codex");
  return setKeyAction(
    profile,
    layer,
    group,
    index,
    enabled ? { kind: "codex" } : { kind: "unassigned" }
  );
}

export function setActiveLayerCount(profile: DeviceProfile, count: number): DeviceProfile {
  if (!Number.isInteger(count) || count < 2 || count > 6) {
    throw new RangeError("Active layer count must be between 2 and 6");
  }
  return { ...profile, activeLayerCount: count };
}

export function toRoutingConfig(profile: DeviceProfile): RoutingConfig {
  const layer1 = profile.layers[0];
  const layer1_command_targets = layer1.keys.command.map((key) =>
    key.action.kind === "codex" ? 0 : 7
  );
  const groupOrder: readonly EditableControlGroup[] = [
    "agent",
    "command",
    "encoder",
    "joystick"
  ];
  const higher_codex_masks = profile.layers.slice(1).map((layer) =>
    groupOrder.map((group) =>
      layer.keys[group].reduce(
        (mask, key, index) =>
          key.action.kind === "codex" ? mask | (1 << index) : mask,
        0
      )
    )
  );
  return {
    enabled: true,
    layer_count: profile.activeLayerCount,
    layer1_command_targets,
    higher_codex_masks
  };
}
