export type AppUpdatePhase =
  | "idle"
  | "checking"
  | "up-to-date"
  | "available"
  | "downloading"
  | "downloaded"
  | "error";

export interface AppUpdateSnapshot {
  phase: AppUpdatePhase;
  currentVersion: string;
  targetVersion: string | null;
  percent: number | null;
  error: string | null;
}

export interface AppUpdaterAdapter {
  autoDownload: boolean;
  autoInstallOnAppQuit: boolean;
  on(event: string, listener: (...args: any[]) => void): unknown;
  checkForUpdates(): Promise<unknown>;
  downloadUpdate(): Promise<unknown>;
  quitAndInstall(isSilent?: boolean, isForceRunAfter?: boolean): void;
}

function message(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export class AppUpdateController {
  private state: AppUpdateSnapshot;

  constructor(
    private readonly updater: AppUpdaterAdapter,
    currentVersion: string,
    private readonly publish: (state: AppUpdateSnapshot) => void
  ) {
    this.state = {
      phase: "idle",
      currentVersion,
      targetVersion: null,
      percent: null,
      error: null
    };
    updater.autoDownload = false;
    updater.autoInstallOnAppQuit = true;
    updater.on("checking-for-update", () => this.patch({ phase: "checking" }));
    updater.on("update-not-available", () => this.patch({
      phase: "up-to-date", targetVersion: null, percent: null
    }));
    updater.on("update-available", (info: { version?: unknown }) => this.patch({
      phase: "available",
      targetVersion: typeof info?.version === "string" ? info.version : null,
      percent: null
    }));
    updater.on("download-progress", (progress: { percent?: unknown }) => this.patch({
      phase: "downloading",
      percent: typeof progress?.percent === "number"
        ? Math.max(0, Math.min(100, Math.round(progress.percent)))
        : null
    }));
    updater.on("update-downloaded", (info: { version?: unknown }) => this.patch({
      phase: "downloaded",
      targetVersion: typeof info?.version === "string"
        ? info.version
        : this.state.targetVersion,
      percent: 100
    }));
    updater.on("error", (error: unknown) => this.patch({
      phase: "error", error: message(error), percent: null
    }));
  }

  get snapshot(): Readonly<AppUpdateSnapshot> {
    return this.state;
  }

  async check(): Promise<AppUpdateSnapshot> {
    this.patch({ phase: "checking", error: null });
    try {
      await this.updater.checkForUpdates();
    } catch (error) {
      this.patch({ phase: "error", error: message(error), percent: null });
    }
    return this.state;
  }

  async download(): Promise<AppUpdateSnapshot> {
    if (this.state.phase !== "available") {
      throw new Error("No application update is available");
    }
    this.patch({ phase: "downloading", percent: 0, error: null });
    try {
      await this.updater.downloadUpdate();
    } catch (error) {
      this.patch({ phase: "error", error: message(error), percent: null });
    }
    return this.state;
  }

  install(): void {
    if (this.state.phase !== "downloaded") {
      throw new Error("Application update has not finished downloading");
    }
    this.updater.quitAndInstall(false, true);
  }

  private patch(patch: Partial<AppUpdateSnapshot>): void {
    this.state = { ...this.state, ...patch };
    this.publish({ ...this.state });
  }
}
