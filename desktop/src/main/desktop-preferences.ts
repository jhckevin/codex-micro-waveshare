import { promises as fs } from "node:fs";
import path from "node:path";

export interface DesktopPreferences {
  minimizeToTray: boolean;
  launchAtLogin: boolean;
  automaticAppUpdates: boolean;
}

export const DEFAULT_DESKTOP_PREFERENCES: DesktopPreferences = {
  minimizeToTray: true,
  launchAtLogin: false,
  automaticAppUpdates: true
};

export function decodeDesktopPreferences(value: unknown): DesktopPreferences {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new TypeError("Malformed desktop preferences");
  }
  const record = value as Record<string, unknown>;
  for (const key of [
    "minimizeToTray",
    "launchAtLogin",
    "automaticAppUpdates"
  ] as const) {
    if (typeof record[key] !== "boolean") {
      throw new TypeError(`Malformed desktop preference: ${key}`);
    }
  }
  return {
    minimizeToTray: record.minimizeToTray as boolean,
    launchAtLogin: record.launchAtLogin as boolean,
    automaticAppUpdates: record.automaticAppUpdates as boolean
  };
}

export class DesktopPreferencesRepository {
  constructor(private readonly filePath: string) {}

  async load(): Promise<DesktopPreferences> {
    try {
      const text = await fs.readFile(this.filePath, "utf8");
      return decodeDesktopPreferences(JSON.parse(text));
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== "ENOENT") {
        // Corrupt or obsolete preferences must never prevent the control
        // application from opening. Replace them with a known-safe baseline.
        await this.save(DEFAULT_DESKTOP_PREFERENCES);
      }
      return { ...DEFAULT_DESKTOP_PREFERENCES };
    }
  }

  async save(value: unknown): Promise<DesktopPreferences> {
    const preferences = decodeDesktopPreferences(value);
    await fs.mkdir(path.dirname(this.filePath), { recursive: true });
    const temporary = `${this.filePath}.tmp`;
    await fs.writeFile(temporary, `${JSON.stringify(preferences, null, 2)}\n`, {
      encoding: "utf8",
      mode: 0o600
    });
    await fs.rename(temporary, this.filePath);
    return preferences;
  }
}
