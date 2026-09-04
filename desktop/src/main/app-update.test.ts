import { describe, expect, it, vi } from "vitest";

import {
  AppUpdateController,
  type AppUpdaterAdapter
} from "./app-update";

class FakeUpdater implements AppUpdaterAdapter {
  autoDownload = true;
  autoInstallOnAppQuit = false;
  readonly listeners = new Map<string, (...args: any[]) => void>();
  checkForUpdates = vi.fn(async () => undefined);
  downloadUpdate = vi.fn(async () => undefined);
  quitAndInstall = vi.fn();
  on(event: string, listener: (...args: any[]) => void): unknown {
    this.listeners.set(event, listener);
    return this;
  }
  emit(event: string, value?: unknown): void {
    this.listeners.get(event)?.(value);
  }
}

describe("application updater state machine", () => {
  it("requires explicit download and publishes bounded progress", async () => {
    const updater = new FakeUpdater();
    const states: string[] = [];
    const controller = new AppUpdateController(
      updater, "1.0.1", (state) => states.push(state.phase)
    );
    expect(updater.autoDownload).toBe(false);
    expect(updater.autoInstallOnAppQuit).toBe(true);
    updater.emit("update-available", { version: "1.0.2" });
    await controller.download();
    updater.emit("download-progress", { percent: 140.2 });
    updater.emit("update-downloaded", { version: "1.0.2" });
    expect(controller.snapshot).toMatchObject({
      phase: "downloaded", targetVersion: "1.0.2", percent: 100
    });
    controller.install();
    expect(updater.quitAndInstall).toHaveBeenCalledWith(false, true);
    expect(states).toContain("downloading");
  });

  it("contains network failures instead of rejecting the UI request", async () => {
    const updater = new FakeUpdater();
    updater.checkForUpdates.mockRejectedValueOnce(new Error("offline"));
    const controller = new AppUpdateController(updater, "1.0.1", () => undefined);
    await expect(controller.check()).resolves.toMatchObject({
      phase: "error", error: "offline"
    });
  });
});
