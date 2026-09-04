import { describe, expect, it } from "vitest";

import {
  DeviceStore,
  type DeviceBridge,
  type DeviceStoreSnapshot
} from "./device-store";
import type { DeviceRequest, DeviceTransportKind } from "../shared/protocol";


class Bridge implements DeviceBridge {
  status: Record<string, unknown> = {
    status: "disconnected",
    transport: "usb",
    protocol: null,
    lastError: null
  };
  device: Record<string, unknown> = {
    ok: true,
    layer: 1,
    connected: true,
    transport_connected: true,
    transport: "usb",
    bluetooth_enabled: true,
    ble_slot: 1,
    battery_present: true,
    battery_percent: 64,
    battery_charging: true,
    battery_external_power: true,
    battery_voltage_mv: 3910,
    battery_charge_limit_ma: 500,
    brightness: 80,
    volume: 40,
    sound: "linear",
    sound_enabled: true,
    standby_timeout_seconds: 180,
    super_standby_timeout_seconds: 10800,
    auto_ultra_timeout_seconds: 18000,
    anti_accidental_shutdown: false,
    power_button_mode: "connected_standby",
    ultra_touch_wake: false,
    animation_strength: 70,
    joystick_sensitivity_percent: 100,
    protocol_profile: "auto",
    commands: []
  };
  readonly calls: string[] = [];
  failNextRequest = false;

  async getStatus(): Promise<unknown> {
    this.calls.push("status");
    return this.status;
  }
  async connect(kind: DeviceTransportKind): Promise<unknown> {
    this.calls.push(`connect:${kind}`);
    this.status = {
      status: "connected",
      transport: kind,
      protocol: 1,
      lastError: null
    };
    this.device.transport = kind;
    return this.status;
  }
  async disconnect(): Promise<void> {
    this.calls.push("disconnect");
    this.status = {
      status: "disconnected",
      transport: this.status.transport,
      protocol: null,
      lastError: null
    };
  }
  async request(message: DeviceRequest): Promise<unknown> {
    this.calls.push(message.method);
    if (this.failNextRequest) {
      this.failNextRequest = false;
      throw new Error("apply failed");
    }
    if (message.method === "cfg.get_state") return this.device;
    if (message.method === "cfg.set_display") {
      this.device.brightness = message.params?.brightness;
    }
    return { ok: true };
  }
}

function connected(snapshot: DeviceStoreSnapshot) {
  if (snapshot.device === null) throw new Error("expected device state");
  return snapshot.device;
}

describe("DeviceStore", () => {
  it("connects through the selected transport and refreshes device state", async () => {
    const bridge = new Bridge();
    const store = new DeviceStore(bridge);

    await store.connect("usb");

    expect(bridge.calls).toEqual(["connect:usb", "cfg.get_state"]);
    expect(store.snapshot.session.status).toBe("connected");
    expect(connected(store.snapshot)).toMatchObject({
      batteryPresent: true,
      batteryPercent: 64,
      batteryCharging: true,
      batteryExternalPower: true,
      batteryVoltageMv: 3910,
      chargeLimitMa: 500,
      chargePowerLimitMw: 2050
    });
  });

  it("silently refreshes live battery state while connected", async () => {
    const bridge = new Bridge();
    const store = new DeviceStore(bridge);
    await store.connect("usb");
    bridge.calls.length = 0;
    bridge.device.battery_percent = 51;
    bridge.device.battery_charging = false;
    bridge.device.battery_external_power = false;
    bridge.device.battery_voltage_mv = 3770;

    await store.refreshDeviceState();

    expect(bridge.calls).toEqual(["cfg.get_state"]);
    expect(connected(store.snapshot)).toMatchObject({
      batteryPercent: 51,
      batteryCharging: false,
      batteryExternalPower: false,
      batteryVoltageMv: 3770,
      chargePowerLimitMw: 0
    });
    expect(store.snapshot.busy).toBe(false);
  });

  it("does not poll device state while disconnected", async () => {
    const bridge = new Bridge();
    const store = new DeviceStore(bridge);

    await store.refreshDeviceState();

    expect(bridge.calls).toEqual([]);
  });

  it("discards a stale live refresh after a setting mutation", async () => {
    const bridge = new Bridge();
    const store = new DeviceStore(bridge);
    await store.connect("usb");
    const stale = { ...bridge.device, brightness: 80 };
    let resolveRefresh: ((value: unknown) => void) | undefined;
    bridge.request = async (request) => {
      bridge.calls.push(request.method);
      if (request.method === "cfg.get_state") {
        return new Promise((resolve) => {
          resolveRefresh = resolve;
        });
      }
      return { ok: true };
    };

    const refresh = store.refreshDeviceState();
    await Promise.resolve();
    await store.setDisplayBrightness(30);
    if (resolveRefresh === undefined) throw new Error("refresh did not start");
    resolveRefresh(stale);
    await refresh;

    expect(connected(store.snapshot).brightness).toBe(30);
  });

  it("refreshes a previously connected session on startup", async () => {
    const bridge = new Bridge();
    bridge.status.status = "connected";
    bridge.status.protocol = 1;
    const store = new DeviceStore(bridge);

    await store.refresh();

    expect(bridge.calls).toEqual(["status", "cfg.get_state"]);
    expect(connected(store.snapshot).bleSlot).toBe(1);
  });

  it("rolls an optimistic setting back when the device rejects it", async () => {
    const bridge = new Bridge();
    const store = new DeviceStore(bridge);
    await store.connect("usb");
    bridge.failNextRequest = true;
    const observed: number[] = [];
    const unsubscribe = store.subscribe((snapshot) => {
      if (snapshot.device !== null) {
        observed.push(snapshot.device.brightness);
      }
    });

    await expect(
      store.setDisplayBrightness(30)
    ).rejects.toThrow("apply failed");

    unsubscribe();
    expect(observed).toContain(30);
    expect(connected(store.snapshot).brightness).toBe(80);
    expect(store.snapshot.error).toBe("apply failed");
  });

  it("disconnects and reconnects using the last selected transport", async () => {
    const bridge = new Bridge();
    const store = new DeviceStore(bridge);
    await store.connect("ble");
    bridge.calls.length = 0;

    await store.reconnect();

    expect(bridge.calls).toEqual([
      "disconnect",
      "connect:ble",
      "cfg.get_state"
    ]);
    expect(store.snapshot.session.transport).toBe("ble");
  });

  it("rejects malformed battery data instead of displaying a fake value", async () => {
    const bridge = new Bridge();
    bridge.device.battery_percent = 255;
    const store = new DeviceStore(bridge);

    await expect(store.connect("usb")).rejects.toThrow(
      "Malformed device configuration state"
    );

    expect(store.snapshot.device).toBeNull();
  });

  it("maps every settings control to the matching firmware method", async () => {
    const bridge = new Bridge();
    const requests: DeviceRequest[] = [];
    bridge.request = async (request) => {
      requests.push(request);
      if (request.method === "cfg.get_state") return bridge.device;
      return { ok: true };
    };
    const store = new DeviceStore(bridge);
    await store.connect("usb");
    requests.length = 0;

    await store.setSound({
      profile: "tactile",
      volume: 60,
      enabled: false
    });
    await store.setTransport("mixed");
    await store.setBluetooth(false);
    await store.setBleSlot(3);
    await store.clearBond(3);
    await store.setDisplay({
      brightness: 70,
      screensaverTimeoutSeconds: 600,
      animationStrength: 80,
      autoShutdownTimeoutSeconds: 7200,
      antiAccidentalShutdown: true,
      smartScreensaverEnabled: false
    });
    await store.setPower({
      powerButtonMode: "ultra_standby",
      autoUltraTimeoutSeconds: 18000,
      ultraTouchWake: true
    });
    await store.setJoystickSensitivity(140);
    await store.setProtocolProfile("current");

    expect(requests).toEqual([
      {
        method: "cfg.set_sound",
        params: { value: "tactile", volume: 60, enabled: false }
      },
      {
        method: "cfg.set_transport",
        params: { value: "mixed" }
      },
      {
        method: "cfg.set_bluetooth",
        params: { enabled: false }
      },
      {
        method: "cfg.set_ble_slot",
        params: { value: 3 }
      },
      {
        method: "cfg.clear_bond",
        params: { slot: 3 }
      },
      {
        method: "cfg.set_display",
        params: {
          brightness: 70,
          screensaver_timeout_seconds: 600,
          animation_strength: 80,
          auto_shutdown_timeout_seconds: 7200,
          anti_accidental_shutdown: true,
          smart_screensaver_enabled: false
        }
      },
      {
        method: "cfg.set_power",
        params: {
          power_button_mode: "ultra_standby",
          auto_ultra_timeout_seconds: 18000,
          ultra_touch_wake: true
        }
      },
      {
        method: "cfg.set_input",
        params: { joystick_sensitivity_percent: 140 }
      },
      {
        method: "cfg.set_protocol_profile",
        params: { value: "current" }
      }
    ]);
    expect(connected(store.snapshot)).toMatchObject({
      sound: "tactile",
      volume: 60,
      soundEnabled: false,
      transport: "mixed",
      bluetoothEnabled: false,
      bleSlot: 3,
      brightness: 70,
      standbyTimeoutSeconds: 600,
      superStandbyTimeoutSeconds: 7200,
      animationStrength: 80,
      antiAccidentalShutdown: true,
      smartScreensaverEnabled: false,
      powerButtonMode: "ultra_standby",
      autoUltraTimeoutSeconds: 18000,
      ultraTouchWake: true,
      joystickSensitivityPercent: 140,
      protocolProfile: "current"
    });
  });
});
