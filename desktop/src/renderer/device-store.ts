import type {
  DeviceRequest,
  DeviceTransportKind
} from "../shared/protocol";


export interface DeviceBridge {
  getStatus(): Promise<unknown>;
  connect(kind: DeviceTransportKind): Promise<unknown>;
  disconnect(): Promise<void>;
  request(message: DeviceRequest): Promise<unknown>;
}

export type ClientSessionStatus =
  | "disconnected"
  | "connecting"
  | "connected"
  | "disconnecting";

export interface ClientSessionState {
  status: ClientSessionStatus;
  transport: DeviceTransportKind;
  protocol: number | null;
  lastError: string | null;
}

export interface DeviceConfigState {
  layer: number;
  codexConnected: boolean;
  transportConnected: boolean;
  transport: "usb" | "ble" | "mixed" | "auto";
  bluetoothEnabled: boolean;
  bleSlot: number;
  batteryPresent: boolean;
  batteryPercent: number;
  batteryCharging: boolean;
  batteryExternalPower: boolean;
  batteryVoltageMv: number;
  chargeLimitMa: number;
  chargePowerLimitMw: number;
  brightness: number;
  volume: number;
  sound: "linear" | "tactile" | "clicky";
  soundEnabled: boolean;
  standbyTimeoutSeconds: number;
  smartScreensaverEnabled: boolean;
  superStandbyTimeoutSeconds: number;
  autoUltraTimeoutSeconds: number;
  antiAccidentalShutdown: boolean;
  powerButtonMode: "connected_standby" | "ultra_standby";
  ultraTouchWake: boolean;
  animationStrength: number;
  joystickSensitivityPercent: number;
  protocolProfile: "auto" | "current" | "legacy" | "compatibility";
}

export interface DeviceStoreSnapshot {
  session: ClientSessionState;
  device: DeviceConfigState | null;
  busy: boolean;
  error: string | null;
}

function record(value: unknown): Record<string, unknown> | undefined {
  return typeof value === "object" && value !== null && !Array.isArray(value)
    ? value as Record<string, unknown>
    : undefined;
}

function integer(
  value: unknown,
  minimum: number,
  maximum: number
): value is number {
  return Number.isInteger(value) &&
    Number(value) >= minimum &&
    Number(value) <= maximum;
}

function oneOf<T extends string>(
  value: unknown,
  values: readonly T[]
): value is T {
  return typeof value === "string" && values.includes(value as T);
}

export function decodeClientSession(value: unknown): ClientSessionState {
  const state = record(value);
  if (
    state === undefined ||
    !oneOf(state.status, [
      "disconnected",
      "connecting",
      "connected",
      "disconnecting"
    ] as const) ||
    !oneOf(state.transport, ["usb", "ble"] as const) ||
    !(state.protocol === null || state.protocol === 1) ||
    !(state.lastError === null || typeof state.lastError === "string")
  ) {
    throw new TypeError("Malformed device session state");
  }
  return {
    status: state.status,
    transport: state.transport,
    protocol: state.protocol,
    lastError: state.lastError
  };
}

export function decodeDeviceConfig(value: unknown): DeviceConfigState {
  const state = record(value);
  if (
    state === undefined ||
    state.ok !== true ||
    !integer(state.layer, 1, 6) ||
    typeof state.connected !== "boolean" ||
    typeof state.transport_connected !== "boolean" ||
    !oneOf(state.transport, ["usb", "ble", "mixed", "auto"] as const) ||
    typeof state.bluetooth_enabled !== "boolean" ||
    !integer(state.ble_slot, 1, 3) ||
    typeof state.battery_present !== "boolean" ||
    !integer(state.battery_percent, 0, 100) ||
    typeof state.battery_charging !== "boolean" ||
    !(
      state.battery_external_power === undefined ||
      typeof state.battery_external_power === "boolean"
    ) ||
    !integer(state.battery_voltage_mv, 0, 6000) ||
    !integer(state.battery_charge_limit_ma, 0, 2000) ||
    !integer(state.brightness, 10, 100) ||
    !integer(state.volume, 10, 100) ||
    !oneOf(state.sound, ["linear", "tactile", "clicky"] as const) ||
    typeof state.sound_enabled !== "boolean" ||
    !integer(state.standby_timeout_seconds, 0, 86400) ||
    !integer(state.super_standby_timeout_seconds, 0, 86400) ||
    !(state.smart_screensaver_enabled === undefined ||
      typeof state.smart_screensaver_enabled === "boolean") ||
    !integer(state.auto_ultra_timeout_seconds, 0, 86400) ||
    typeof state.anti_accidental_shutdown !== "boolean" ||
    !oneOf(
      state.power_button_mode,
      ["connected_standby", "ultra_standby"] as const
    ) ||
    typeof state.ultra_touch_wake !== "boolean" ||
    !integer(state.animation_strength, 0, 100) ||
    !integer(state.joystick_sensitivity_percent, 50, 200) ||
    !oneOf(
      state.protocol_profile,
      ["auto", "current", "legacy", "compatibility"] as const
    )
  ) {
    throw new TypeError("Malformed device configuration state");
  }
  return {
    layer: state.layer,
    codexConnected: state.connected,
    transportConnected: state.transport_connected,
    transport: state.transport,
    bluetoothEnabled: state.bluetooth_enabled,
    bleSlot: state.ble_slot,
    batteryPresent: state.battery_present,
    batteryPercent: state.battery_percent,
    batteryCharging: state.battery_charging,
    // Older firmware did not expose VBUS separately.  Charging implies VBUS,
    // so this fallback remains correct without inventing a "full" state.
    batteryExternalPower:
      typeof state.battery_external_power === "boolean"
        ? state.battery_external_power
        : state.battery_charging,
    batteryVoltageMv: state.battery_voltage_mv,
    chargeLimitMa: state.battery_charge_limit_ma,
    chargePowerLimitMw:
      state.battery_present && state.battery_charging
        ? Math.round(4100 * Number(state.battery_charge_limit_ma) / 1000)
        : 0,
    brightness: state.brightness,
    volume: state.volume,
    sound: state.sound,
    soundEnabled: state.sound_enabled,
    standbyTimeoutSeconds: state.standby_timeout_seconds,
    smartScreensaverEnabled: state.smart_screensaver_enabled !== false,
    superStandbyTimeoutSeconds: state.super_standby_timeout_seconds,
    autoUltraTimeoutSeconds: state.auto_ultra_timeout_seconds,
    antiAccidentalShutdown: state.anti_accidental_shutdown,
    powerButtonMode: state.power_button_mode,
    ultraTouchWake: state.ultra_touch_wake,
    animationStrength: state.animation_strength,
    joystickSensitivityPercent: state.joystick_sensitivity_percent,
    protocolProfile: state.protocol_profile
  };
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export class DeviceStore {
  private listeners = new Set<(snapshot: DeviceStoreSnapshot) => void>();
  private deviceMutationEpoch = 0;
  private deviceRefreshInFlight = false;
  private current: DeviceStoreSnapshot = {
    session: {
      status: "disconnected",
      transport: "usb",
      protocol: null,
      lastError: null
    },
    device: null,
    busy: false,
    error: null
  };

  constructor(private readonly bridge: DeviceBridge) {}

  get snapshot(): Readonly<DeviceStoreSnapshot> {
    return this.current;
  }

  subscribe(listener: (snapshot: DeviceStoreSnapshot) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async connect(kind: DeviceTransportKind): Promise<void> {
    ++this.deviceMutationEpoch;
    this.update({
      busy: true,
      error: null,
      session: { ...this.current.session, status: "connecting", transport: kind }
    });
    try {
      const session = decodeClientSession(await this.bridge.connect(kind));
      const device = decodeDeviceConfig(
        await this.bridge.request({ method: "cfg.get_state" })
      );
      this.update({ session, device, busy: false });
    } catch (error) {
      this.update({
        busy: false,
        device: null,
        error: errorMessage(error)
      });
      throw error;
    }
  }

  async disconnect(): Promise<void> {
    ++this.deviceMutationEpoch;
    this.update({ busy: true, error: null });
    try {
      await this.bridge.disconnect();
      this.update({
        busy: false,
        device: null,
        session: {
          ...this.current.session,
          status: "disconnected",
          protocol: null
        }
      });
    } catch (error) {
      this.update({ busy: false, error: errorMessage(error) });
      throw error;
    }
  }

  async refresh(): Promise<void> {
    this.update({ busy: true, error: null });
    try {
      const session = decodeClientSession(await this.bridge.getStatus());
      const device = session.status === "connected"
        ? decodeDeviceConfig(
            await this.bridge.request({ method: "cfg.get_state" })
          )
        : null;
      this.update({ session, device, busy: false });
    } catch (error) {
      this.update({ busy: false, device: null, error: errorMessage(error) });
      throw error;
    }
  }

  async refreshDeviceState(): Promise<void> {
    if (
      this.deviceRefreshInFlight ||
      this.current.busy ||
      this.current.session.status !== "connected" ||
      this.current.device === null
    ) {
      return;
    }
    const epoch = this.deviceMutationEpoch;
    this.deviceRefreshInFlight = true;
    try {
      const device = decodeDeviceConfig(
        await this.bridge.request({ method: "cfg.get_state" })
      );
      if (
        epoch === this.deviceMutationEpoch &&
        this.current.session.status === "connected"
      ) {
        this.update({ device, error: null });
      }
    } catch (error) {
      if (
        epoch === this.deviceMutationEpoch &&
        this.current.session.status === "connected"
      ) {
        this.update({ error: errorMessage(error) });
      }
      throw error;
    } finally {
      this.deviceRefreshInFlight = false;
    }
  }

  async reconnect(): Promise<void> {
    const transport = this.current.session.transport;
    await this.disconnect();
    await this.connect(transport);
  }

  async setSound(options: {
    profile: DeviceConfigState["sound"];
    volume: number;
    enabled: boolean;
  }): Promise<void> {
    if (!integer(options.volume, 10, 100) || options.volume % 10 !== 0) {
      throw new RangeError("Volume must be a 10 percent step");
    }
    await this.applySetting(
      "cfg.set_sound",
      {
        value: options.profile,
        volume: options.volume,
        enabled: options.enabled
      },
      {
        sound: options.profile,
        volume: options.volume,
        soundEnabled: options.enabled
      }
    );
  }

  async setTransport(
    transport: DeviceConfigState["transport"]
  ): Promise<void> {
    await this.applySetting(
      "cfg.set_transport",
      { value: transport },
      { transport }
    );
  }

  async setBluetooth(enabled: boolean): Promise<void> {
    await this.applySetting(
      "cfg.set_bluetooth",
      { enabled },
      { bluetoothEnabled: enabled }
    );
  }

  async setBleSlot(slot: number): Promise<void> {
    if (!integer(slot, 1, 3)) throw new RangeError("Invalid BLE slot");
    await this.applySetting(
      "cfg.set_ble_slot",
      { value: slot },
      { bleSlot: slot }
    );
  }

  async clearBond(slot: number): Promise<void> {
    if (!integer(slot, 1, 3)) throw new RangeError("Invalid BLE slot");
    await this.applySetting("cfg.clear_bond", { slot }, {});
  }

  async setDisplay(options: {
    brightness: number;
    screensaverTimeoutSeconds: number;
    animationStrength: number;
    autoShutdownTimeoutSeconds: number;
    antiAccidentalShutdown: boolean;
    smartScreensaverEnabled?: boolean;
  }): Promise<void> {
    if (
      !integer(options.brightness, 10, 100) ||
      options.brightness % 10 !== 0 ||
      !integer(options.screensaverTimeoutSeconds, 0, 86400) ||
      !integer(options.animationStrength, 0, 100) ||
      ![0, 3600, 7200, 10800, 18000]
        .includes(options.autoShutdownTimeoutSeconds)
    ) {
      throw new RangeError("Invalid display or shutdown setting");
    }
    const smartScreensaverEnabled = options.smartScreensaverEnabled ??
      this.requireDevice().smartScreensaverEnabled;
    await this.applySetting(
      "cfg.set_display",
      {
        brightness: options.brightness,
        screensaver_timeout_seconds: options.screensaverTimeoutSeconds,
        animation_strength: options.animationStrength,
        auto_shutdown_timeout_seconds: options.autoShutdownTimeoutSeconds,
        anti_accidental_shutdown: options.antiAccidentalShutdown,
        smart_screensaver_enabled: smartScreensaverEnabled
      },
      {
        brightness: options.brightness,
        standbyTimeoutSeconds: options.screensaverTimeoutSeconds,
        animationStrength: options.animationStrength,
        superStandbyTimeoutSeconds: options.autoShutdownTimeoutSeconds,
        antiAccidentalShutdown: options.antiAccidentalShutdown,
        smartScreensaverEnabled
      }
    );
  }

  async setPower(options: {
    powerButtonMode: DeviceConfigState["powerButtonMode"];
    autoUltraTimeoutSeconds: number;
    ultraTouchWake: boolean;
  }): Promise<void> {
    if (
      ![0, 3600, 10800, 18000, 28800, 43200]
        .includes(options.autoUltraTimeoutSeconds)
    ) {
      throw new RangeError("Invalid automatic ultra standby timeout");
    }
    await this.applySetting(
      "cfg.set_power",
      {
        power_button_mode: options.powerButtonMode,
        auto_ultra_timeout_seconds: options.autoUltraTimeoutSeconds,
        ultra_touch_wake: options.ultraTouchWake
      },
      {
        powerButtonMode: options.powerButtonMode,
        autoUltraTimeoutSeconds: options.autoUltraTimeoutSeconds,
        ultraTouchWake: options.ultraTouchWake
      }
    );
  }

  async setJoystickSensitivity(value: number): Promise<void> {
    if (!integer(value, 50, 200) || value % 10 !== 0) {
      throw new RangeError("Joystick sensitivity must be a 10 percent step");
    }
    await this.applySetting(
      "cfg.set_input",
      { joystick_sensitivity_percent: value },
      { joystickSensitivityPercent: value }
    );
  }

  async setProtocolProfile(
    profile: DeviceConfigState["protocolProfile"]
  ): Promise<void> {
    await this.applySetting(
      "cfg.set_protocol_profile",
      { value: profile },
      { protocolProfile: profile }
    );
  }

  async setDisplayBrightness(brightness: number): Promise<void> {
    if (!integer(brightness, 10, 100) || brightness % 10 !== 0) {
      throw new RangeError("Brightness must be a 10 percent step");
    }
    ++this.deviceMutationEpoch;
    const previous = this.requireDevice();
    this.update({
      device: { ...previous, brightness },
      error: null
    });
    try {
      const reply = record(await this.bridge.request({
        method: "cfg.set_display",
        params: { brightness }
      }));
      if (reply?.ok !== true) throw new Error("Device rejected display setting");
    } catch (error) {
      this.update({ device: previous, error: errorMessage(error) });
      throw error;
    }
  }

  private async applySetting(
    method: string,
    params: Record<string, unknown>,
    patch: Partial<DeviceConfigState>
  ): Promise<void> {
    ++this.deviceMutationEpoch;
    const previous = this.requireDevice();
    this.update({
      device: { ...previous, ...patch },
      error: null
    });
    try {
      const reply = record(await this.bridge.request({ method, params }));
      if (reply?.ok !== true) {
        throw new Error(`Device rejected ${method}`);
      }
    } catch (error) {
      this.update({ device: previous, error: errorMessage(error) });
      throw error;
    }
  }

  private requireDevice(): DeviceConfigState {
    if (this.current.device === null) {
      throw new Error("Device is not connected");
    }
    return this.current.device;
  }

  private update(patch: Partial<DeviceStoreSnapshot>): void {
    this.current = { ...this.current, ...patch };
    for (const listener of this.listeners) listener(this.current);
  }
}
