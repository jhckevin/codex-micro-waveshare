import { contextBridge, ipcRenderer } from "electron";

import type {
  AppControlEvent,
  DeviceRequest,
  DeviceTransportKind
} from "../shared/protocol";

export interface CodexMicroBridge {
  getDesktopPreferences(): Promise<unknown>;
  setDesktopPreferences(preferences: unknown): Promise<unknown>;
  showDesktopWindow(): Promise<void>;
  quitDesktopApp(): Promise<void>;
  getAppUpdateStatus(): Promise<unknown>;
  checkAppUpdate(): Promise<unknown>;
  downloadAppUpdate(): Promise<unknown>;
  installAppUpdate(): Promise<void>;
  onAppUpdateStatus(listener: (status: unknown) => void): () => void;
  getStatus(): Promise<unknown>;
  connect(kind: DeviceTransportKind): Promise<unknown>;
  disconnect(): Promise<void>;
  request(message: DeviceRequest): Promise<unknown>;
  loadProfile(): Promise<unknown>;
  saveProfile(profile: unknown): Promise<unknown>;
  listCustomIcons(): Promise<Array<{ id: string; alpha: Uint8Array }>>;
  installCustomIcon(icon: {
    id: string;
    alpha: Uint8Array;
  }): Promise<unknown>;
  removeCustomIcon(id: string): Promise<unknown>;
  installContentPack(pack: Uint8Array): Promise<unknown>;
  cancelContentInstall(): Promise<unknown>;
  inspectUpdates(): Promise<unknown>;
  installUpdate(url: string): Promise<unknown>;
  installLatestUpdate(
    packageClass:
      | "compatibility"
      | "service_reload"
      | "complete_firmware"
      | "user_content"
  ): Promise<unknown>;
  confirmUpdate(reboot: boolean): Promise<unknown>;
  cancelUpdate(): Promise<unknown>;
  onControlEvent(listener: (event: AppControlEvent) => void): () => void;
  onUpdateProgress(listener: (progress: unknown) => void): () => void;
  onContentProgress(listener: (progress: unknown) => void): () => void;
}

const bridge: CodexMicroBridge = {
  getDesktopPreferences: () => ipcRenderer.invoke("desktop:get-preferences"),
  setDesktopPreferences: (preferences) =>
    ipcRenderer.invoke("desktop:set-preferences", preferences),
  showDesktopWindow: () => ipcRenderer.invoke("desktop:show"),
  quitDesktopApp: () => ipcRenderer.invoke("desktop:quit"),
  getAppUpdateStatus: () => ipcRenderer.invoke("app-update:get-status"),
  checkAppUpdate: () => ipcRenderer.invoke("app-update:check"),
  downloadAppUpdate: () => ipcRenderer.invoke("app-update:download"),
  installAppUpdate: () => ipcRenderer.invoke("app-update:install"),
  onAppUpdateStatus: (listener) => {
    const handler = (_event: Electron.IpcRendererEvent, status: unknown) =>
      listener(status);
    ipcRenderer.on("app-update:status", handler);
    return () => ipcRenderer.removeListener("app-update:status", handler);
  },
  getStatus: () => ipcRenderer.invoke("device:get-status"),
  connect: (kind) => ipcRenderer.invoke("device:connect", kind),
  disconnect: () => ipcRenderer.invoke("device:disconnect"),
  request: (message) => ipcRenderer.invoke("device:request", message),
  loadProfile: () => ipcRenderer.invoke("profile:get"),
  saveProfile: (profile) => ipcRenderer.invoke("profile:save", profile),
  listCustomIcons: () => ipcRenderer.invoke("content:list-icons"),
  installCustomIcon: (icon) =>
    ipcRenderer.invoke("content:install-icon", icon),
  removeCustomIcon: (id) =>
    ipcRenderer.invoke("content:remove-icon", id),
  installContentPack: (pack) =>
    ipcRenderer.invoke("content:install-pack", pack),
  cancelContentInstall: () => ipcRenderer.invoke("content:cancel"),
  inspectUpdates: () => ipcRenderer.invoke("update:inspect"),
  installUpdate: (url) => ipcRenderer.invoke("update:install-url", url),
  installLatestUpdate: (packageClass) =>
    ipcRenderer.invoke("update:install-latest", packageClass),
  confirmUpdate: (reboot) => ipcRenderer.invoke("update:confirm", reboot),
  cancelUpdate: () => ipcRenderer.invoke("update:cancel"),
  onControlEvent: (listener) => {
    const handler = (_event: Electron.IpcRendererEvent, control: AppControlEvent) =>
      listener(control);
    ipcRenderer.on("device:control", handler);
    return () => ipcRenderer.removeListener("device:control", handler);
  },
  onUpdateProgress: (listener) => {
    const handler = (_event: Electron.IpcRendererEvent, progress: unknown) =>
      listener(progress);
    ipcRenderer.on("update:progress", handler);
    return () => ipcRenderer.removeListener("update:progress", handler);
  },
  onContentProgress: (listener) => {
    const handler = (_event: Electron.IpcRendererEvent, progress: unknown) =>
      listener(progress);
    ipcRenderer.on("content:progress", handler);
    return () => ipcRenderer.removeListener("content:progress", handler);
  }
};

contextBridge.exposeInMainWorld("codexMicro", bridge);
