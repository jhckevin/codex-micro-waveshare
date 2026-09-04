export const APP_PROTOCOL_VERSION = 1 as const;
export const APP_HEARTBEAT_MS = 500 as const;
export const APP_LEASE_MS = 1500 as const;
export const APP_PRIVATE_CONTROL_ID_MAX = 41 as const;

export type DeviceTransportKind = "usb" | "ble";
export type ControlGroup = "agent" | "command" | "encoder" | "joystick";

export interface AppControlEvent {
  type: "control";
  group: ControlGroup;
  id: number;
  action: 0 | 1 | 2 | 3;
  direction?: -1 | 1;
  angle_milli?: number;
  distance_milli?: number;
}

export interface RoutingConfig {
  enabled: boolean;
  layer_count: number;
  layer1_command_targets: readonly number[];
  higher_codex_masks: readonly (readonly number[])[];
}

export interface DeviceRequest {
  method: string;
  params?: Record<string, unknown>;
}

export interface DeviceReply {
  ok: boolean;
  method?: string;
  protocol?: typeof APP_PROTOCOL_VERSION;
  heartbeat_ms?: number;
  lease_ms?: number;
  error?: string;
  result?: unknown;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

export function decodeDeviceReply(value: unknown): DeviceReply {
  if (
    !isRecord(value) ||
    typeof value.ok !== "boolean" ||
    (value.method !== undefined && typeof value.method !== "string")
  ) {
    throw new TypeError("Malformed device reply");
  }
  if (value.protocol !== undefined && value.protocol !== APP_PROTOCOL_VERSION) {
    throw new RangeError(`Unsupported device protocol: ${String(value.protocol)}`);
  }
  if (value.error !== undefined && typeof value.error !== "string") {
    throw new TypeError("Malformed device error");
  }
  return value as unknown as DeviceReply;
}

export function decodeAppControlEvent(value: unknown): AppControlEvent {
  const groups: readonly ControlGroup[] = [
    "agent",
    "command",
    "encoder",
    "joystick"
  ];
  if (
    !isRecord(value) ||
    value.event !== "input" ||
    !groups.includes(value.group as ControlGroup) ||
    !Number.isInteger(value.id) ||
    Number(value.id) < 0 ||
    Number(value.id) > APP_PRIVATE_CONTROL_ID_MAX ||
    !Number.isInteger(value.action) ||
    Number(value.action) < 0 ||
    Number(value.action) > 3 ||
    "layer" in value
  ) {
    throw new TypeError("Malformed private control event");
  }
  const group = value.group as ControlGroup;
  const id = Number(value.id);
  const action = Number(value.action) as AppControlEvent["action"];
  if (action === 2) {
    if (
      group !== "encoder" ||
      (value.direction !== -1 && value.direction !== 1)
    ) {
      throw new TypeError("Malformed private encoder event");
    }
    return {
      type: "control",
      group,
      id,
      action,
      direction: value.direction
    };
  }
  if (action === 3) {
    if (
      group !== "joystick" ||
      !Number.isInteger(value.angle_milli) ||
      Number(value.angle_milli) < 0 ||
      Number(value.angle_milli) > 1000 ||
      !Number.isInteger(value.distance_milli) ||
      Number(value.distance_milli) < 0 ||
      Number(value.distance_milli) > 1000
    ) {
      throw new TypeError("Malformed private joystick event");
    }
    return {
      type: "control",
      group,
      id,
      action,
      angle_milli: Number(value.angle_milli),
      distance_milli: Number(value.distance_milli)
    };
  }
  return { type: "control", group, id, action };
}

export function isUsbUpdateTransport(kind: DeviceTransportKind): boolean {
  return kind === "usb";
}

export function appHelloRequest(): DeviceRequest {
  return { method: "app.hello", params: { protocol: APP_PROTOCOL_VERSION } };
}

export function appHeartbeatRequest(): DeviceRequest {
  return { method: "app.heartbeat", params: { protocol: APP_PROTOCOL_VERSION } };
}

export function appGoodbyeRequest(): DeviceRequest {
  return { method: "app.goodbye", params: { protocol: APP_PROTOCOL_VERSION } };
}
