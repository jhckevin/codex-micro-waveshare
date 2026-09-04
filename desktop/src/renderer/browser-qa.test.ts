import { describe, expect, it, vi } from "vitest";

import { createBrowserQaBridge } from "./browser-qa";

describe("browser QA bridge", () => {
  it("models a connected USB device with battery and charging telemetry", async () => {
    const bridge = createBrowserQaBridge();
    await expect(bridge.connect("usb")).resolves.toMatchObject({
      status: "connected",
      transport: "usb",
      protocol: 1
    });
    await expect(bridge.request({ method: "cfg.get_state" })).resolves.toMatchObject({
      ok: true,
      battery_present: true,
      battery_percent: 73,
      battery_charging: true,
      battery_external_power: true,
      battery_voltage_mv: 4015
    });
  });

  it("persists setting mutations for the next device refresh", async () => {
    const bridge = createBrowserQaBridge();
    await bridge.request({
      method: "cfg.set_power",
      params: {
        power_button_mode: "ultra_standby",
        auto_ultra_timeout_seconds: 43200,
        ultra_touch_wake: true
      }
    });
    await expect(bridge.request({ method: "cfg.get_state" })).resolves.toMatchObject({
      power_button_mode: "ultra_standby",
      auto_ultra_timeout_seconds: 43200,
      ultra_touch_wake: true
    });
    await bridge.request({
      method: "cfg.set_display",
      params: { smart_screensaver_enabled: false }
    });
    await expect(bridge.request({ method: "cfg.get_state" })).resolves.toMatchObject({
      smart_screensaver_enabled: false
    });
  });

  it("models persisted desktop preferences and application updates", async () => {
    const bridge = createBrowserQaBridge();
    await bridge.setDesktopPreferences({
      minimizeToTray: false,
      launchAtLogin: true,
      automaticAppUpdates: false
    });
    await expect(bridge.getDesktopPreferences()).resolves.toMatchObject({
      minimizeToTray: false,
      launchAtLogin: true,
      automaticAppUpdates: false
    });
    await expect(bridge.checkAppUpdate()).resolves.toMatchObject({
      phase: "up-to-date",
      currentVersion: "1.0.1"
    });
  });

  it("emits a completed encrypted update transfer awaiting confirmation", async () => {
    const bridge = createBrowserQaBridge();
    const listener = vi.fn();
    bridge.onUpdateProgress(listener);
    await expect(bridge.installLatestUpdate("compatibility")).resolves.toMatchObject({
      state: "awaiting_confirmation",
      expected_offset: 262144,
      total_size: 262144,
      target_version: "1.0.1.2"
    });
    expect(listener).toHaveBeenCalledOnce();
  });
});
