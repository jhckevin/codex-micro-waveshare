import {
  windowsPresetAction,
  type WindowsAction
} from "./action-engine";
import type {
  AppControlEvent,
  ControlGroup,
  RoutingConfig
} from "../shared/protocol";

const GROUP_COUNTS: Readonly<Record<ControlGroup, number>> = {
  agent: 6,
  command: 6,
  encoder: 1,
  joystick: 1
};

const SHORTCUT_PRESETS = new Set([
  "copy",
  "paste",
  "cut",
  "windows-key",
  "click-to-do",
  "codex",
  "chatgpt",
  "always-on-top",
  "typeless",
  "typeless-hold",
  "typeless-press",
  "voice-typing",
  "command-palette"
]);

type RuntimeIcon =
  | { kind: "builtin"; id: string }
  | { kind: "custom"; id: string }
  | {
      kind: "inherit";
      layer: number;
      group: ControlGroup;
      index: number;
    }
  | { kind: "blank" };

type RuntimeAction =
  | { kind: "codex" }
  | { kind: "unassigned" }
  | { kind: "shortcut"; preset: string }
  | { kind: "custom-chord"; keys: string[] }
  | { kind: "custom-pulse"; keys: string[] }
  | { kind: "launch-app"; target: string };

export interface RuntimeKeyDefinition {
  icon: RuntimeIcon;
  action: RuntimeAction;
}

export interface RuntimeLayerDefinition {
  number: number;
  name: string;
  keys: Record<ControlGroup, RuntimeKeyDefinition[]>;
}

export interface RuntimeDeviceProfile {
  activeLayerCount: number;
  layers: RuntimeLayerDefinition[];
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasOnlyKeys(
  value: Record<string, unknown>,
  expected: readonly string[]
): boolean {
  const actual = Object.keys(value).sort();
  return actual.length === expected.length &&
    actual.every((key, index) => key === [...expected].sort()[index]);
}

function boundedText(value: unknown, maximum: number): value is string {
  return typeof value === "string" && value.length > 0 && value.length <= maximum &&
    !/[\u0000-\u001f]/.test(value);
}

function isIcon(value: unknown): value is RuntimeIcon {
  if (!isRecord(value) || typeof value.kind !== "string") return false;
  if (value.kind === "blank") return hasOnlyKeys(value, ["kind"]);
  if (value.kind === "builtin" || value.kind === "custom") {
    return hasOnlyKeys(value, ["kind", "id"]) && boundedText(value.id, 96);
  }
  if (value.kind !== "inherit" ||
      !hasOnlyKeys(value, ["kind", "layer", "group", "index"])) return false;
  const group = value.group;
  return Number.isInteger(value.layer) &&
    Number(value.layer) >= 1 &&
    Number(value.layer) <= 6 &&
    typeof group === "string" &&
    Object.prototype.hasOwnProperty.call(GROUP_COUNTS, group) &&
    Number.isInteger(value.index) &&
    Number(value.index) >= 0 &&
    Number(value.index) < GROUP_COUNTS[group as ControlGroup];
}

function isAction(value: unknown): value is RuntimeAction {
  if (!isRecord(value) || typeof value.kind !== "string") return false;
  if (value.kind === "codex" || value.kind === "unassigned") {
    return hasOnlyKeys(value, ["kind"]);
  }
  if (value.kind === "shortcut") {
    return hasOnlyKeys(value, ["kind", "preset"]) &&
      typeof value.preset === "string" &&
      SHORTCUT_PRESETS.has(value.preset);
  }
  if (value.kind === "custom-chord" ||
      value.kind === "custom-pulse") {
    return hasOnlyKeys(value, ["kind", "keys"]) &&
      Array.isArray(value.keys) &&
      value.keys.length >= 1 &&
      value.keys.length <= 8 &&
      value.keys.every((key) => boundedText(key, 32));
  }
  if (value.kind === "launch-app") {
    return hasOnlyKeys(value, ["kind", "target"]) &&
      boundedText(value.target, 1024);
  }
  return false;
}

function isKey(value: unknown): value is RuntimeKeyDefinition {
  return isRecord(value) &&
    hasOnlyKeys(value, ["icon", "action"]) &&
    isIcon(value.icon) &&
    isAction(value.action);
}

export function isRuntimeDeviceProfile(
  value: unknown
): value is RuntimeDeviceProfile {
  if (!isRecord(value) ||
      !hasOnlyKeys(value, ["activeLayerCount", "layers"]) ||
      !Number.isInteger(value.activeLayerCount) ||
      Number(value.activeLayerCount) < 2 ||
      Number(value.activeLayerCount) > 6 ||
      !Array.isArray(value.layers) ||
      value.layers.length !== 6) return false;

  const groups = Object.keys(GROUP_COUNTS) as ControlGroup[];
  return value.layers.every((layerValue, layerIndex) => {
    if (!isRecord(layerValue) ||
        !hasOnlyKeys(layerValue, ["number", "name", "keys"]) ||
        layerValue.number !== layerIndex + 1 ||
        !boundedText(layerValue.name, 64) ||
        !isRecord(layerValue.keys) ||
        !hasOnlyKeys(layerValue.keys, groups)) return false;
    const layerKeys = layerValue.keys;
    return groups.every((group) => {
      const keys = layerKeys[group];
      return Array.isArray(keys) &&
        keys.length === GROUP_COUNTS[group] &&
        keys.every(isKey);
    });
  });
}

function cloneProfile(profile: RuntimeDeviceProfile): RuntimeDeviceProfile {
  return JSON.parse(JSON.stringify(profile)) as RuntimeDeviceProfile;
}

function defaultIcon(
  layer: number,
  group: ControlGroup,
  index: number
): RuntimeIcon {
  if (layer > 1) return { kind: "inherit", layer: 1, group, index };
  if (group === "command") {
    return {
      kind: "builtin",
      id: ["bolt", "check", "close", "computer-use", "microphone", "codex"][index]
    };
  }
  if (group === "agent") {
    return { kind: "builtin", id: `agent-${index + 1}` };
  }
  return { kind: "builtin", id: group };
}

export function createDefaultRuntimeProfile(): RuntimeDeviceProfile {
  const groups = Object.keys(GROUP_COUNTS) as ControlGroup[];
  return {
    activeLayerCount: 6,
    layers: Array.from({ length: 6 }, (_, layerIndex) => {
      const layer = layerIndex + 1;
      return {
        number: layer,
        name: layer === 1 ? "Codex Classical" : `Layer ${layer}`,
        keys: Object.fromEntries(groups.map((group) => [
          group,
          Array.from({ length: GROUP_COUNTS[group] }, (_, index) => ({
            icon: defaultIcon(layer, group, index),
            action: { kind: "codex" as const }
          }))
        ])) as Record<ControlGroup, RuntimeKeyDefinition[]>
      };
    })
  };
}

export function toRuntimeRoutingConfig(
  profile: RuntimeDeviceProfile
): RoutingConfig {
  if (!isRuntimeDeviceProfile(profile)) {
    throw new TypeError("profile validation failed");
  }
  const groups: readonly ControlGroup[] = [
    "agent",
    "command",
    "encoder",
    "joystick"
  ];
  return {
    enabled: true,
    layer_count: profile.activeLayerCount,
    layer1_command_targets: profile.layers[0].keys.command.map((key) =>
      key.action.kind === "codex" ? 0 : 7
    ),
    higher_codex_masks: profile.layers.slice(1).map((layer) =>
      groups.map((group) =>
        layer.keys[group].reduce(
          (mask, key, index) =>
            key.action.kind === "codex" ? mask | (1 << index) : mask,
          0
        )
      )
    )
  };
}

function eventLocation(event: AppControlEvent): {
  layer: number;
  index: number;
} | undefined {
  if (event.id >= 36) {
    if (event.group !== "command" || event.id > 41) return undefined;
    return { layer: 1, index: event.id - 36 };
  }
  return {
    layer: Math.floor(event.id / 6) + 1,
    index: event.group === "agent" || event.group === "command"
      ? event.id % 6
      : 0
  };
}

export class ProfileRuntime {
  private current: RuntimeDeviceProfile;

  constructor(profile: unknown = createDefaultRuntimeProfile()) {
    if (!isRuntimeDeviceProfile(profile)) {
      throw new TypeError("profile validation failed");
    }
    this.current = cloneProfile(profile);
  }

  get profile(): RuntimeDeviceProfile {
    return cloneProfile(this.current);
  }

  replace(profile: unknown): RuntimeDeviceProfile {
    if (!isRuntimeDeviceProfile(profile)) {
      throw new TypeError("profile validation failed");
    }
    this.current = cloneProfile(profile);
    return this.profile;
  }

  resolve(event: AppControlEvent): WindowsAction {
    const location = eventLocation(event);
    if (location === undefined || location.layer > this.current.layers.length) {
      return { kind: "unassigned" };
    }
    const keys = this.current.layers[location.layer - 1].keys[event.group];
    const key = keys[location.index];
    if (key === undefined) return { kind: "unassigned" };
    const action = key.action;
    if (action.kind === "shortcut") {
      return windowsPresetAction(action.preset);
    }
    if (action.kind === "custom-chord") {
      return { kind: "chord", keys: [...action.keys] };
    }
    if (action.kind === "custom-pulse") {
      return { kind: "pulse", keys: [...action.keys] };
    }
    if (action.kind === "launch-app") {
      return { kind: "focus-app", target: action.target };
    }
    return { kind: "unassigned" };
  }
}
