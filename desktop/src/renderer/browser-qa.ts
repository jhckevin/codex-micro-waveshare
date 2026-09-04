import type { CodexMicroBridge } from "../preload";
import type { DeviceRequest, DeviceTransportKind } from "../shared/protocol";

type Listener = (value: unknown) => void;

const QA_MARKER = "codex-micro-browser-device-qa";

function ok(): { ok: true } {
  return { ok: true };
}

export function createBrowserQaBridge(): CodexMicroBridge {
  let connected = false;
  let transport: DeviceTransportKind = "usb";
  let profile: unknown = {};
  let desktopPreferences = {
    minimizeToTray: true,
    launchAtLogin: false,
    automaticAppUpdates: true
  };
  const updateListeners = new Set<Listener>();
  const contentListeners = new Set<Listener>();
  const controlListeners = new Set<Listener>();
  const appUpdateListeners = new Set<Listener>();
  let appUpdate: {
    phase: string;
    currentVersion: string;
    targetVersion: string | null;
    percent: number | null;
    error: string | null;
  } = {
    phase: "up-to-date",
    currentVersion: "1.0.1",
    targetVersion: null,
    percent: null,
    error: null
  };
  const state: Record<string, unknown> = {
    ok: true,
    layer: 1,
    connected: true,
    transport_connected: true,
    transport: "usb",
    bluetooth_enabled: true,
    ble_slot: 2,
    battery_present: true,
    battery_percent: 73,
    battery_charging: true,
    battery_external_power: true,
    battery_voltage_mv: 4015,
    battery_charge_limit_ma: 500,
    brightness: 80,
    volume: 40,
    sound: "tactile",
    sound_enabled: true,
    standby_timeout_seconds: 180,
    smart_screensaver_enabled: true,
    super_standby_timeout_seconds: 7200,
    auto_ultra_timeout_seconds: 10800,
    anti_accidental_shutdown: false,
    power_button_mode: "connected_standby",
    ultra_touch_wake: false,
    animation_strength: 70,
    joystick_sensitivity_percent: 120,
    protocol_profile: "auto"
  };

  const session = () => ({
    status: connected ? "connected" : "disconnected",
    transport,
    protocol: connected ? 1 : null,
    lastError: null
  });

  const patchFromRequest = (message: DeviceRequest): void => {
    const params = message.params ?? {};
    const mappings: Record<string, Record<string, string>> = {
      "cfg.set_sound": {
        value: "sound", volume: "volume", enabled: "sound_enabled"
      },
      "cfg.set_transport": { value: "transport" },
      "cfg.set_bluetooth": { enabled: "bluetooth_enabled" },
      "cfg.set_ble_slot": { value: "ble_slot" },
      "cfg.set_display": {
        brightness: "brightness",
        screensaver_timeout_seconds: "standby_timeout_seconds",
        animation_strength: "animation_strength",
        auto_shutdown_timeout_seconds: "super_standby_timeout_seconds",
        anti_accidental_shutdown: "anti_accidental_shutdown",
        smart_screensaver_enabled: "smart_screensaver_enabled"
      },
      "cfg.set_power": {
        power_button_mode: "power_button_mode",
        auto_ultra_timeout_seconds: "auto_ultra_timeout_seconds",
        ultra_touch_wake: "ultra_touch_wake"
      },
      "cfg.set_input": {
        joystick_sensitivity_percent: "joystick_sensitivity_percent"
      },
      "cfg.set_protocol_profile": { value: "protocol_profile" }
    };
    const mapping = mappings[message.method];
    if (!mapping) return;
    for (const [source, target] of Object.entries(mapping)) {
      if (source in params) state[target] = params[source];
    }
  };

  return {
    getDesktopPreferences: async () => ({ ...desktopPreferences }),
    setDesktopPreferences: async (value) => {
      desktopPreferences = value as typeof desktopPreferences;
      return { ...desktopPreferences };
    },
    showDesktopWindow: async () => undefined,
    quitDesktopApp: async () => undefined,
    getAppUpdateStatus: async () => ({ ...appUpdate }),
    checkAppUpdate: async () => ({ ...appUpdate }),
    downloadAppUpdate: async () => {
      appUpdate = { ...appUpdate, phase: "downloaded", percent: 100 };
      appUpdateListeners.forEach((listener) => listener(appUpdate));
      return { ...appUpdate };
    },
    installAppUpdate: async () => undefined,
    onAppUpdateStatus: (listener) => {
      appUpdateListeners.add(listener);
      return () => appUpdateListeners.delete(listener);
    },
    getStatus: async () => session(),
    connect: async (kind) => {
      connected = true;
      transport = kind;
      state.transport = kind;
      return session();
    },
    disconnect: async () => { connected = false; },
    request: async (message) => {
      if (message.method === "cfg.get_state") return { ...state };
      patchFromRequest(message);
      return ok();
    },
    loadProfile: async () => profile,
    saveProfile: async (value) => { profile = value; return ok(); },
    listCustomIcons: async () => [],
    installCustomIcon: async () => ok(),
    removeCustomIcon: async () => ok(),
    installContentPack: async (pack) => {
      contentListeners.forEach((listener) => listener({
        state: "complete", expected_offset: pack.byteLength,
        total_size: pack.byteLength
      }));
      return ok();
    },
    cancelContentInstall: async () => ok(),
    inspectUpdates: async () => ({
      provisioned: true,
      device_id: "qa-device-73b2d901"
    }),
    installUpdate: async () => ok(),
    installLatestUpdate: async () => {
      const progress = {
        state: "awaiting_confirmation",
        expected_offset: 262144,
        total_size: 262144,
        target_version: "1.0.1.2"
      };
      updateListeners.forEach((listener) => listener(progress));
      return progress;
    },
    confirmUpdate: async () => ok(),
    cancelUpdate: async () => ok(),
    onControlEvent: (listener) => {
      controlListeners.add(listener as Listener);
      return () => controlListeners.delete(listener as Listener);
    },
    onUpdateProgress: (listener) => {
      updateListeners.add(listener);
      return () => updateListeners.delete(listener);
    },
    onContentProgress: (listener) => {
      contentListeners.add(listener);
      return () => contentListeners.delete(listener);
    }
  };
}

export function installBrowserQaBridge(): void {
  if (window.codexMicro) return;
  Object.defineProperty(window, "codexMicro", {
    configurable: true,
    value: createBrowserQaBridge()
  });
  document.documentElement.dataset.qaBridge = QA_MARKER;
}
