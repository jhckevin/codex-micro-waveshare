import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { describe, expect, it } from "vitest";

import {
  DEFAULT_DESKTOP_PREFERENCES,
  DesktopPreferencesRepository,
  decodeDesktopPreferences
} from "./desktop-preferences";

describe("desktop preferences", () => {
  it("rejects partial and incorrectly typed renderer input", () => {
    expect(() => decodeDesktopPreferences({ minimizeToTray: true }))
      .toThrow("Malformed desktop preference");
    expect(() => decodeDesktopPreferences({
      minimizeToTray: true,
      launchAtLogin: "yes",
      automaticAppUpdates: true
    })).toThrow("launchAtLogin");
  });

  it("uses safe defaults when no settings exist", async () => {
    const root = await mkdtemp(path.join(os.tmpdir(), "codex-desktop-"));
    const repository = new DesktopPreferencesRepository(
      path.join(root, "desktop.json")
    );
    await expect(repository.load()).resolves.toEqual(DEFAULT_DESKTOP_PREFERENCES);
  });

  it("replaces corrupt data and persists changes atomically", async () => {
    const root = await mkdtemp(path.join(os.tmpdir(), "codex-desktop-"));
    const file = path.join(root, "desktop.json");
    await writeFile(file, "not-json", "utf8");
    const repository = new DesktopPreferencesRepository(file);
    await expect(repository.load()).resolves.toEqual(DEFAULT_DESKTOP_PREFERENCES);
    const saved = await repository.save({
      minimizeToTray: false,
      launchAtLogin: true,
      automaticAppUpdates: false
    });
    expect(JSON.parse(await readFile(file, "utf8"))).toEqual(saved);
  });
});
