import {
  app,
  BrowserWindow,
  ipcMain,
  Menu,
  nativeImage,
  session,
  Tray
} from "electron";
import path from "node:path";
import { CodexBleTransport } from "./ble-transport";
import { DeviceSession } from "./device-session";
import { CodexUsbCdcTransport } from "./usb-cdc-transport";
import { DeviceUpdateRelay, type EncryptedUpdatePackage } from "./update-session";
import { DeviceContentSession } from "./content-session";
import { ContentLibraryRepository } from "./content-library";
import {
  compileUserContentPack,
  type UserContentIcon
} from "./content-compiler";
import { ActionEngine, DeviceActionBridge } from "./action-engine";
import {
  NativeInputProcessChannel,
  WindowsInput
} from "./windows-input";
import { ProfileRepository } from "./profile-repository";
import { ProfileController } from "./profile-controller";
import {
  canonicalPackageUrl,
  updateServiceEndpoint
} from "./update-service-url";
import { fetchBoundedJson } from "./bounded-json-fetch";
import {
  isRuntimeDeviceProfile,
  type RuntimeDeviceProfile
} from "./profile-runtime";
import type { DeviceTransportKind } from "../shared/protocol";
import {
  releaseSmokeOutput,
  writeReleaseSmokeMarker
} from "./release-smoke";
import {
  DesktopPreferencesRepository,
  type DesktopPreferences
} from "./desktop-preferences";
import { autoUpdater } from "electron-updater";
import { AppUpdateController } from "./app-update";

type AppTransport = CodexUsbCdcTransport | CodexBleTransport;

const UPDATE_CONTROL_LIMITS = {
  maximumBytes: 8 * 1024,
  timeoutMs: 10_000
} as const;
const UPDATE_PACKAGE_LIMITS = {
  maximumBytes: 16 * 1024 * 1024,
  timeoutMs: 60_000
} as const;

let transport: AppTransport | undefined;
let deviceSession: DeviceSession | undefined;
let updateRelay: DeviceUpdateRelay | undefined;
let contentSession: DeviceContentSession | undefined;
let actionBridge: DeviceActionBridge | undefined;
let nativeInput: NativeInputProcessChannel | undefined;
let removeRendererControlListener: (() => void) | undefined;
let removeTrayStatusListener: (() => void) | undefined;
let profileController: ProfileController;
let contentRepository: ContentLibraryRepository;
let contentIcons: UserContentIcon[] = [];
let mainWindow: BrowserWindow | undefined;
let tray: Tray | undefined;
let quitting = false;
let desktopPreferences: DesktopPreferences;
let desktopPreferencesRepository: DesktopPreferencesRepository;
let appUpdateController: AppUpdateController | undefined;
const releaseSmokeMarker = releaseSmokeOutput(process.argv);

const hasSingleInstanceLock = app.requestSingleInstanceLock();
if (!hasSingleInstanceLock) app.quit();

function showMainWindow(): void {
  if (mainWindow === undefined || mainWindow.isDestroyed()) {
    createWindow();
    return;
  }
  if (mainWindow.isMinimized()) mainWindow.restore();
  mainWindow.show();
  mainWindow.focus();
}

function rebuildTrayMenu(): void {
  if (tray === undefined) return;
  const connected = deviceSession?.snapshot.status === "connected";
  tray.setToolTip(
    connected
      ? `Codex Micro Control · ${deviceSession?.snapshot.transport.toUpperCase()} 已连接`
      : "Codex Micro Control · 等待设备"
  );
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: "打开 Codex Micro Control", click: showMainWindow },
    {
      label: connected ? "设备已连接" : "设备未连接",
      enabled: false
    },
    { type: "separator" },
    {
      label: "开机时启动",
      type: "checkbox",
      checked: desktopPreferences.launchAtLogin,
      click: (item) => {
        void saveDesktopPreferences({
          ...desktopPreferences,
          launchAtLogin: item.checked
        });
      }
    },
    { type: "separator" },
    {
      label: "退出",
      click: () => {
        quitting = true;
        app.quit();
      }
    }
  ]));
}

async function saveDesktopPreferences(
  value: unknown
): Promise<DesktopPreferences> {
  desktopPreferences = await desktopPreferencesRepository.save(value);
  app.setLoginItemSettings({
    openAtLogin: desktopPreferences.launchAtLogin,
    args: ["--background"]
  });
  rebuildTrayMenu();
  return desktopPreferences;
}

async function closeCurrentDevice(): Promise<void> {
  await actionBridge?.close();
  await deviceSession?.disconnect();
  nativeInput?.close();
  removeRendererControlListener?.();
  removeTrayStatusListener?.();
  actionBridge = undefined;
  nativeInput = undefined;
  removeRendererControlListener = undefined;
  removeTrayStatusListener = undefined;
  deviceSession = undefined;
  updateRelay = undefined;
  contentSession = undefined;
  transport = undefined;
}

function createWindow(): void {
  if (mainWindow !== undefined && !mainWindow.isDestroyed()) {
    showMainWindow();
    return;
  }
  const window = new BrowserWindow({
    width: 1320,
    height: 860,
    minWidth: 1040,
    minHeight: 700,
    backgroundColor: "#f4f5f2",
    show: releaseSmokeMarker === undefined && !process.argv.includes("--background"),
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      backgroundThrottling: true,
      preload: path.join(__dirname, "../preload/index.js")
    }
  });
  mainWindow = window;
  window.removeMenu();
  window.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  window.webContents.on("will-navigate", (event, url) => {
    if (!url.startsWith("file:")) event.preventDefault();
  });
  window.on("close", (event) => {
    if (!quitting && releaseSmokeMarker === undefined &&
        desktopPreferences.minimizeToTray) {
      event.preventDefault();
      window.hide();
    }
  });
  window.on("closed", () => {
    if (mainWindow === window) mainWindow = undefined;
  });
  if (releaseSmokeMarker !== undefined) {
    window.webContents.once("did-finish-load", () => {
      void writeReleaseSmokeMarker(releaseSmokeMarker, {
        schema: 1,
        packaged: app.isPackaged,
        platform: process.platform,
        arch: process.arch,
        package_version: app.getVersion(),
        renderer_loaded: true
      }).then(() => {
        window.destroy();
        app.exit(0);
      }).catch((error: unknown) => {
        console.error("release smoke marker failed", error);
        app.exit(70);
      });
    });
  }
  void window.loadFile(
    path.join(__dirname, "../../dist-renderer/index.html")
  ).catch((error: unknown) => {
    console.error("renderer load failed", error);
    if (releaseSmokeMarker !== undefined) app.exit(71);
  });
}

app.on("second-instance", showMainWindow);

app.whenReady().then(async () => {
  if (!hasSingleInstanceLock) return;
  session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => {
    callback(false);
  });
  session.defaultSession.setPermissionCheckHandler(() => false);
  desktopPreferencesRepository = new DesktopPreferencesRepository(
    path.join(app.getPath("userData"), "desktop-preferences.json")
  );
  desktopPreferences = await desktopPreferencesRepository.load();
  appUpdateController = new AppUpdateController(
    autoUpdater,
    app.getVersion(),
    (state) => {
      for (const window of BrowserWindow.getAllWindows()) {
        if (!window.isDestroyed()) window.webContents.send("app-update:status", state);
      }
    }
  );
  app.setLoginItemSettings({
    openAtLogin: desktopPreferences.launchAtLogin,
    args: ["--background"]
  });
  const trayIconPath = app.isPackaged
    ? path.join(process.resourcesPath, "build", "app-icon.png")
    : path.join(__dirname, "../../build/app-icon.png");
  const trayIcon = nativeImage.createFromPath(trayIconPath)
    .resize({ width: 20, height: 20 });
  tray = new Tray(trayIcon);
  tray.on("click", showMainWindow);
  rebuildTrayMenu();
  profileController = new ProfileController(
    new ProfileRepository<RuntimeDeviceProfile>(
      path.join(app.getPath("userData"), "device-profile.json"),
      isRuntimeDeviceProfile
    )
  );
  await profileController.initialize();
  contentRepository = new ContentLibraryRepository(
    path.join(app.getPath("userData"), "keycap-content.json")
  );
  contentIcons = await contentRepository.load();
  ipcMain.handle("device:get-status", () => deviceSession?.snapshot ?? ({
    status: "disconnected", transport: "usb", protocol: null, lastError: null
  }));
  ipcMain.handle("device:connect", async (
    _event,
    kind: DeviceTransportKind = "usb"
  ) => {
    if (kind !== "usb" && kind !== "ble") throw new Error("Invalid transport");
    if (process.platform !== "win32") {
      throw new Error("The Codex Micro client supports Windows only");
    }
    if (deviceSession?.snapshot.status === "connected") {
      if (deviceSession.snapshot.transport === kind) return deviceSession.snapshot;
      await closeCurrentDevice();
    }
    const helperDirectory = path.join(process.resourcesPath, "native");
    transport = kind === "usb"
      ? await CodexUsbCdcTransport.open(
          path.join(helperDirectory, "codex-usb-cdc-helper.ps1")
        )
      : await CodexBleTransport.open(
          path.join(helperDirectory, "codex-ble-helper.exe"),
          undefined,
          helperDirectory
        );
    removeRendererControlListener = transport.onEvent((control) => {
      for (const window of BrowserWindow.getAllWindows()) {
        if (!window.isDestroyed()) window.webContents.send("device:control", control);
      }
    });
    deviceSession = new DeviceSession(transport);
    removeTrayStatusListener = deviceSession.subscribe(rebuildTrayMenu);
    updateRelay = kind === "usb" ? new DeviceUpdateRelay(transport) : undefined;
    contentSession =
      kind === "usb" ? new DeviceContentSession(transport) : undefined;
    const helperPath = path.join(helperDirectory, "codex-input-helper.exe");
    nativeInput = NativeInputProcessChannel.open(
      helperPath,
      undefined,
      helperDirectory
    );
    const input = new WindowsInput(nativeInput);
    const engine = new ActionEngine(
      input,
      (control) => profileController.runtime.resolve(control)
    );
    actionBridge = new DeviceActionBridge(
      transport,
      deviceSession,
      engine,
      (error) => {
        for (const window of BrowserWindow.getAllWindows()) {
          if (!window.isDestroyed()) {
            window.webContents.send("device:action-error", error.message);
          }
        }
      }
    );
    try {
      await deviceSession.connect();
      await profileController.apply(deviceSession);
      rebuildTrayMenu();
    } catch (error) {
      await actionBridge.close();
      nativeInput.close();
      removeRendererControlListener?.();
      removeTrayStatusListener?.();
      await transport?.close();
      actionBridge = undefined; nativeInput = undefined;
      removeRendererControlListener = undefined;
      removeTrayStatusListener = undefined;
      deviceSession = undefined;
      updateRelay = undefined;
      contentSession = undefined;
      transport = undefined;
      throw error;
    }
    return deviceSession.snapshot;
  });
  ipcMain.handle("device:disconnect", async () => {
    await closeCurrentDevice();
    rebuildTrayMenu();
  });
  ipcMain.handle("desktop:get-preferences", () => desktopPreferences);
  ipcMain.handle("desktop:set-preferences", (_event, value: unknown) =>
    saveDesktopPreferences(value));
  ipcMain.handle("desktop:show", showMainWindow);
  ipcMain.handle("desktop:quit", () => {
    quitting = true;
    app.quit();
  });
  ipcMain.handle("app-update:get-status", () => appUpdateController?.snapshot);
  ipcMain.handle("app-update:check", () => appUpdateController?.check());
  ipcMain.handle("app-update:download", () => appUpdateController?.download());
  ipcMain.handle("app-update:install", () => appUpdateController?.install());
  ipcMain.handle("device:request", (_event, message) => {
    if (!deviceSession) throw new Error("Device is not connected");
    return deviceSession.request(message);
  });
  ipcMain.handle("profile:get", () => profileController.snapshot);
  ipcMain.handle("profile:save", (_event, profile: unknown) =>
    profileController.save(
      profile,
      deviceSession?.snapshot.status === "connected" ? deviceSession : undefined
    ));
  ipcMain.handle("content:install-pack", async (
    event,
    pack: Uint8Array
  ) => {
    if (!contentSession) {
      throw new Error("Custom keycap content requires native USB");
    }
    if (!(pack instanceof Uint8Array)) {
      throw new TypeError("Invalid keycap content payload");
    }
    const result = await contentSession.install(
      pack,
      (progress) => event.sender.send("content:progress", progress)
    );
    if (deviceSession?.snapshot.status === "connected") {
      await profileController.apply(deviceSession);
    }
    return result;
  });
  ipcMain.handle("content:list-icons", () =>
    contentIcons.map((icon) => ({
      id: icon.id,
      alpha: new Uint8Array(icon.alpha)
    }))
  );
  ipcMain.handle("content:install-icon", async (
    event,
    icon: UserContentIcon
  ) => {
    if (!contentSession) {
      throw new Error("Custom keycap content requires native USB");
    }
    if (typeof icon !== "object" || icon === null ||
        typeof icon.id !== "string" ||
        !(icon.alpha instanceof Uint8Array)) {
      throw new TypeError("Invalid custom keycap icon");
    }
    const candidate = [
      ...contentIcons.filter((item) => item.id !== icon.id),
      { id: icon.id, alpha: new Uint8Array(icon.alpha) }
    ];
    const pack = compileUserContentPack(candidate);
    await contentSession.install(
      pack,
      (progress) => event.sender.send("content:progress", progress)
    );
    contentIcons = await contentRepository.save(candidate);
    if (deviceSession?.snapshot.status === "connected") {
      await profileController.apply(deviceSession);
    }
    return {
      id: icon.id,
      icons: contentIcons.map((item) => item.id)
    };
  });
  ipcMain.handle("content:remove-icon", async (
    event,
    id: string
  ) => {
    if (!contentSession) {
      throw new Error("Custom keycap content requires native USB");
    }
    const candidate = contentIcons.filter((item) => item.id !== id);
    const pack = compileUserContentPack(candidate);
    await contentSession.install(
      pack,
      (progress) => event.sender.send("content:progress", progress)
    );
    contentIcons = await contentRepository.save(candidate);
    if (deviceSession?.snapshot.status === "connected") {
      await profileController.apply(deviceSession);
    }
    return contentIcons.map((item) => item.id);
  });
  ipcMain.handle("content:cancel", () => {
    if (!contentSession) {
      throw new Error("Custom keycap content requires native USB");
    }
    return contentSession.cancel();
  });
  ipcMain.handle("update:inspect", () => {
    if (!updateRelay) throw new Error("Device is not connected over USB");
    return updateRelay.inspect();
  });
  ipcMain.handle("update:install-url", async (event, url: string) => {
    if (!updateRelay) throw new Error("Device is not connected over USB");
    const allowed = canonicalPackageUrl(url);
    const response = await fetchBoundedJson(
      allowed,
      {},
      UPDATE_PACKAGE_LIMITS
    );
    if (!response.ok) throw new Error(`Update download failed: ${response.status}`);
    const updatePackage = response.value as EncryptedUpdatePackage;
    return updateRelay.install(
      updatePackage,
      (progress) => event.sender.send("update:progress", progress));
  });
  ipcMain.handle("update:install-latest", async (
    event,
    packageClass:
      | "compatibility"
      | "service_reload"
      | "complete_firmware"
      | "user_content"
  ) => {
    if (!updateRelay) throw new Error("Device is not connected over USB");
    if (![
      "compatibility",
      "service_reload",
      "complete_firmware",
      "user_content"
    ]
        .includes(packageClass)) {
      throw new Error("Invalid update package class");
    }
    const hello = await updateRelay.inspect();
    if (
      hello.provisioned !== true ||
      typeof hello.device_id !== "string" ||
      !/^[0-9a-f]{32}$/i.test(hello.device_id)
    ) {
      throw new Error("Device secure-update identity is not provisioned");
    }
    const challengeResponse = await fetchBoundedJson(
      updateServiceEndpoint("challenges"),
      {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ device_id: hello.device_id })
      },
      UPDATE_CONTROL_LIMITS
    );
    if (!challengeResponse.ok) {
      throw new Error(`Update authentication failed: ${challengeResponse.status}`);
    }
    const challenge = challengeResponse.value as Record<string, unknown>;
    if (
      typeof challenge.challenge_id !== "string" ||
      typeof challenge.challenge !== "string"
    ) throw new Error("Malformed update challenge");
    const proof = await updateRelay.attest(challenge.challenge);
    const sessionResponse = await fetchBoundedJson(
      updateServiceEndpoint("attest"),
      {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          challenge_id: challenge.challenge_id,
          device_id: proof.device_id,
          tag: proof.tag
        })
      },
      UPDATE_CONTROL_LIMITS
    );
    if (!sessionResponse.ok) {
      throw new Error(`Device attestation failed: ${sessionResponse.status}`);
    }
    const session = sessionResponse.value as Record<string, unknown>;
    if (typeof session.token !== "string") {
      throw new Error("Malformed update session");
    }
    const packageUrl = updateServiceEndpoint("packages/latest");
    packageUrl.searchParams.set("class", packageClass);
    const packageResponse = await fetchBoundedJson(
      packageUrl,
      { headers: { authorization: `Bearer ${session.token}` } },
      UPDATE_PACKAGE_LIMITS
    );
    if (!packageResponse.ok) {
      throw new Error(`Update download failed: ${packageResponse.status}`);
    }
    return updateRelay.install(
      packageResponse.value as EncryptedUpdatePackage,
      (progress) => event.sender.send("update:progress", progress));
  });
  ipcMain.handle("update:confirm", async (_event, reboot: boolean) => {
    if (!updateRelay) throw new Error("Device is not connected over USB");
    const result = await updateRelay.confirm();
    if (reboot) {
      if (!deviceSession) throw new Error("Device session is unavailable");
      await deviceSession.request({ method: "cfg.reboot" });
    }
    return result;
  });
  ipcMain.handle("update:cancel", () => updateRelay?.cancel());
  createWindow();
  if (app.isPackaged && desktopPreferences.automaticAppUpdates) {
    setTimeout(() => { void appUpdateController?.check(); }, 30_000).unref();
    setInterval(() => { void appUpdateController?.check(); }, 6 * 60 * 60 * 1000).unref();
  }
});

app.on("window-all-closed", () => {
  if (quitting) app.quit();
});

app.on("activate", showMainWindow);

let shutdownComplete = false;
app.on("before-quit", (event) => {
  quitting = true;
  if (shutdownComplete) return;
  event.preventDefault();
  void closeCurrentDevice().finally(() => {
    shutdownComplete = true;
    app.quit();
  });
});
