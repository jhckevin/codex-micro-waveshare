import { useEffect, useState } from "react";
import {
  BatteryCharging,
  Bluetooth,
  Gamepad2,
  Monitor,
  Power,
  RotateCcw,
  Volume2
} from "lucide-react";

import { useDevice } from "../device-context";


const screensaverOptions = [0, 30, 60, 180, 600, 1800, 3600];
const shutdownOptions = [0, 3600, 7200, 10800, 18000];
const ultraOptions = [0, 3600, 10800, 18000, 28800, 43200];

interface DesktopPreferences {
  minimizeToTray: boolean;
  launchAtLogin: boolean;
  automaticAppUpdates: boolean;
}

function DesktopSettingsCard({
  preferences,
  onChange
}: {
  preferences: DesktopPreferences | null;
  onChange: (patch: Partial<DesktopPreferences>) => void;
}) {
  if (preferences === null) return null;
  return (
    <section className="settings-card">
      <div className="settings-card-title">
        <Monitor size={18} />
        <div><h2>Windows 客户端</h2><p>控制托盘后台、开机启动和客户端更新。</p></div>
      </div>
      <label className="setting-row">
        <span><strong>关闭到托盘</strong><small>隐藏窗口后保留按键映射与设备连接</small></span>
        <input className="switch" type="checkbox" checked={preferences.minimizeToTray}
          onChange={(event) => onChange({ minimizeToTray: event.target.checked })} />
      </label>
      <label className="setting-row">
        <span><strong>开机时启动</strong><small>以后台模式启动，不主动弹出主窗口</small></span>
        <input className="switch" type="checkbox" checked={preferences.launchAtLogin}
          onChange={(event) => onChange({ launchAtLogin: event.target.checked })} />
      </label>
      <label className="setting-row">
        <span><strong>自动检查客户端更新</strong><small>仅检查；下载和安装仍需要确认</small></span>
        <input className="switch" type="checkbox" checked={preferences.automaticAppUpdates}
          onChange={(event) => onChange({ automaticAppUpdates: event.target.checked })} />
      </label>
    </section>
  );
}

function secondsLabel(value: number): string {
  if (value === 0) return "关闭";
  if (value < 60) return `${value} 秒`;
  if (value < 3600) return `${value / 60} 分钟`;
  return `${value / 3600} 小时`;
}

export function SettingsPage() {
  const { store, snapshot } = useDevice();
  const device = snapshot.device;
  const [desktopPreferences, setDesktopPreferences] =
    useState<DesktopPreferences | null>(null);
  useEffect(() => {
    void window.codexMicro?.getDesktopPreferences().then((value) => {
      if (typeof value === "object" && value !== null) {
        setDesktopPreferences(value as DesktopPreferences);
      }
    });
  }, []);
  const setDesktop = (patch: Partial<DesktopPreferences>) => {
    if (!window.codexMicro || desktopPreferences === null) return;
    const next = { ...desktopPreferences, ...patch };
    setDesktopPreferences(next);
    void window.codexMicro.setDesktopPreferences(next)
      .then((value) => setDesktopPreferences(value as DesktopPreferences))
      .catch(() => setDesktopPreferences(desktopPreferences));
  };

  if (device === null) {
    return (
      <section className="page settings-page">
        <header className="page-header">
          <div>
            <p className="eyebrow">SETTINGS</p>
            <h1>设备设置</h1>
            <p>连接 Codex Micro 后读取设备上的持久化设置。</p>
          </div>
        </header>
        <div className="empty-state">
          <Monitor size={28} />
          <strong>等待设备连接</strong>
          <span>请先回到“设备”页，通过 USB 或 Bluetooth LE 建立 App 会话。</span>
        </div>
        <div className="settings-grid">
          <DesktopSettingsCard preferences={desktopPreferences} onChange={setDesktop} />
        </div>
      </section>
    );
  }

  const run = (operation: Promise<void>) => {
    void operation.catch(() => undefined);
  };
  const setDisplay = (patch: Partial<{
    brightness: number;
    screensaverTimeoutSeconds: number;
    animationStrength: number;
    autoShutdownTimeoutSeconds: number;
    antiAccidentalShutdown: boolean;
    smartScreensaverEnabled: boolean;
  }>) => run(store.setDisplay({
    brightness: device.brightness,
    screensaverTimeoutSeconds: device.standbyTimeoutSeconds,
    animationStrength: device.animationStrength,
    autoShutdownTimeoutSeconds: device.superStandbyTimeoutSeconds,
    antiAccidentalShutdown: device.antiAccidentalShutdown,
    smartScreensaverEnabled: device.smartScreensaverEnabled,
    ...patch
  }));
  const setPower = (patch: Partial<{
    powerButtonMode: typeof device.powerButtonMode;
    autoUltraTimeoutSeconds: number;
    ultraTouchWake: boolean;
  }>) => run(store.setPower({
    powerButtonMode: device.powerButtonMode,
    autoUltraTimeoutSeconds: device.autoUltraTimeoutSeconds,
    ultraTouchWake: device.ultraTouchWake,
    ...patch
  }));

  return (
    <section className="page settings-page">
      <header className="page-header">
        <div>
          <p className="eyebrow">SETTINGS</p>
          <h1>设备设置</h1>
          <p>设置写入设备持久化存储；失败时界面会自动恢复到上一个已确认值。</p>
        </div>
        <div className="settings-battery">
          <BatteryCharging size={18} />
          <span>
            <strong>{device.batteryPresent ? `${device.batteryPercent}%` : "No battery"}</strong>
            <small>
              {device.batteryCharging
                ? `正在充电 · 配置上限 ${(device.chargePowerLimitMw / 1000).toFixed(2)} W`
                : device.batteryExternalPower
                  ? "USB 供电 · 已充满"
                  : "电池供电"}
            </small>
          </span>
        </div>
      </header>

      {snapshot.error && <div className="inline-alert" role="alert">{snapshot.error}</div>}

      <div className="settings-grid">
        <section className="settings-card">
          <div className="settings-card-title">
            <Monitor size={18} />
            <div><h2>显示与动画</h2><p>控制屏幕亮度、自动熄屏和动效强度。</p></div>
          </div>
          <label className="setting-row">
            <span><strong>亮度</strong><small>最低 10%，避免低占空比闪烁</small></span>
            <span className="range-control">
              <input type="range" min="10" max="100" step="10"
                value={device.brightness}
                onChange={(event) => setDisplay({ brightness: Number(event.target.value) })} />
              <output>{device.brightness}%</output>
            </span>
          </label>
          <label className="setting-row">
            <span><strong>自动熄屏</strong><small>无触控后进入 connected standby</small></span>
            <select value={device.standbyTimeoutSeconds}
              onChange={(event) => setDisplay({ screensaverTimeoutSeconds: Number(event.target.value) })}>
              {screensaverOptions.map((value) => <option key={value} value={value}>{secondsLabel(value)}</option>)}
            </select>
          </label>
          <label className="setting-row">
            <span><strong>智能屏保</strong><small>仅在全部灯光熄灭且无操作后开始计时</small></span>
            <input className="switch" type="checkbox" checked={device.smartScreensaverEnabled}
              onChange={(event) => setDisplay({ smartScreensaverEnabled: event.target.checked })} />
          </label>
          <label className="setting-row">
            <span><strong>动画强度</strong><small>不改变 Codex 下发的颜色与灯效类型</small></span>
            <span className="range-control">
              <input type="range" min="0" max="100" step="10"
                value={device.animationStrength}
                onChange={(event) => setDisplay({ animationStrength: Number(event.target.value) })} />
              <output>{device.animationStrength}%</output>
            </span>
          </label>
        </section>

        <section className="settings-card">
          <div className="settings-card-title">
            <Volume2 size={18} />
            <div><h2>按键声音</h2><p>同一预设内的键盘按键使用一致声音。</p></div>
          </div>
          <label className="setting-row">
            <span><strong>声音预设</strong><small>线性、段落或清脆轴体</small></span>
            <select value={device.sound}
              onChange={(event) => run(store.setSound({
                profile: event.target.value as typeof device.sound,
                volume: device.volume,
                enabled: device.soundEnabled
              }))}>
              <option value="linear">Linear</option>
              <option value="tactile">Tactile</option>
              <option value="clicky">Clicky</option>
            </select>
          </label>
          <label className="setting-row">
            <span><strong>音量</strong><small>左下触摸键使用独立的擦声素材</small></span>
            <span className="range-control">
              <input type="range" min="10" max="100" step="10"
                value={device.volume}
                onChange={(event) => run(store.setSound({
                  profile: device.sound,
                  volume: Number(event.target.value),
                  enabled: device.soundEnabled
                }))} />
              <output>{device.volume}%</output>
            </span>
          </label>
          <label className="setting-row">
            <span><strong>启用声音</strong><small>关闭后保留触控与 HID 响应</small></span>
            <input className="switch" type="checkbox" checked={device.soundEnabled}
              onChange={(event) => run(store.setSound({
                profile: device.sound,
                volume: device.volume,
                enabled: event.target.checked
              }))} />
          </label>
        </section>

        <section className="settings-card">
          <div className="settings-card-title">
            <Bluetooth size={18} />
            <div><h2>连接</h2><p>USB 优先；Mixed 允许 USB 与 BLE 同时工作。</p></div>
          </div>
          <label className="setting-row">
            <span><strong>设备传输模式</strong><small>切换不会改变 Codex Classical</small></span>
            <select value={device.transport}
              onChange={(event) => run(store.setTransport(event.target.value as typeof device.transport))}>
              <option value="auto">Auto</option>
              <option value="usb">USB</option>
              <option value="ble">Bluetooth LE</option>
              <option value="mixed">Mixed</option>
            </select>
          </label>
          <label className="setting-row">
            <span><strong>Bluetooth radio</strong><small>关闭以降低 USB 模式功耗</small></span>
            <input className="switch" type="checkbox" checked={device.bluetoothEnabled}
              onChange={(event) => run(store.setBluetooth(event.target.checked))} />
          </label>
          <label className="setting-row">
            <span><strong>配对槽位</strong><small>设备保存三个独立 BLE 主机</small></span>
            <select value={device.bleSlot}
              onChange={(event) => run(store.setBleSlot(Number(event.target.value)))}>
              <option value="1">BLE 1</option>
              <option value="2">BLE 2</option>
              <option value="3">BLE 3</option>
            </select>
          </label>
          <button className="secondary-button danger" type="button"
            onClick={() => run(store.clearBond(device.bleSlot))}>
            <RotateCcw size={15} />清除当前槽位配对
          </button>
        </section>

        <section className="settings-card">
          <div className="settings-card-title">
            <Power size={18} />
            <div><h2>电源策略</h2><p>关机优先于 ultra standby，后者优先于自动熄屏。</p></div>
          </div>
          <label className="setting-row">
            <span><strong>PWR 短按</strong><small>Connected 保持握手；Ultra 追求长续航</small></span>
            <select value={device.powerButtonMode}
              onChange={(event) => setPower({ powerButtonMode: event.target.value as typeof device.powerButtonMode })}>
              <option value="connected_standby">Connected standby</option>
              <option value="ultra_standby">Ultra standby</option>
            </select>
          </label>
          <label className="setting-row">
            <span><strong>自动 Ultra standby</strong><small>已连接但长期未操作时触发</small></span>
            <select value={device.autoUltraTimeoutSeconds}
              onChange={(event) => setPower({ autoUltraTimeoutSeconds: Number(event.target.value) })}>
              {ultraOptions.map((value) => <option key={value} value={value}>{secondsLabel(value)}</option>)}
            </select>
          </label>
          <label className="setting-row">
            <span><strong>Ultra 触控唤醒</strong><small>关闭时仅侧边按键可唤醒</small></span>
            <input className="switch" type="checkbox" checked={device.ultraTouchWake}
              onChange={(event) => setPower({ ultraTouchWake: event.target.checked })} />
          </label>
          <label className="setting-row">
            <span><strong>自动关机</strong><small>需要长按 PWR 才能恢复</small></span>
            <select value={device.superStandbyTimeoutSeconds}
              onChange={(event) => setDisplay({ autoShutdownTimeoutSeconds: Number(event.target.value) })}>
              {shutdownOptions.map((value) => <option key={value} value={value}>{secondsLabel(value)}</option>)}
            </select>
          </label>
          <label className="setting-row">
            <span><strong>防误触自动关机</strong><small>忽略触控活动，只判断连接状态</small></span>
            <input className="switch" type="checkbox" checked={device.antiAccidentalShutdown}
              onChange={(event) => setDisplay({ antiAccidentalShutdown: event.target.checked })} />
          </label>
        </section>

        <section className="settings-card">
          <div className="settings-card-title">
            <Gamepad2 size={18} />
            <div><h2>输入与兼容</h2><p>只影响常规摇杆；Arcade 模式使用独立参数。</p></div>
          </div>
          <label className="setting-row">
            <span><strong>摇杆灵敏度</strong><small>50%–200%，以 10% 为步进</small></span>
            <span className="range-control">
              <input type="range" min="50" max="200" step="10"
                value={device.joystickSensitivityPercent}
                onChange={(event) => run(store.setJoystickSensitivity(Number(event.target.value)))} />
              <output>{device.joystickSensitivityPercent}%</output>
            </span>
          </label>
          <label className="setting-row">
            <span><strong>协议档案</strong><small>Auto 会宽容识别数字/字符串灯效字段</small></span>
            <select value={device.protocolProfile}
              onChange={(event) => run(store.setProtocolProfile(event.target.value as typeof device.protocolProfile))}>
              <option value="auto">Auto</option>
              <option value="current">Current</option>
              <option value="legacy">Legacy</option>
              <option value="compatibility">Compatibility</option>
            </select>
          </label>
        </section>
        <DesktopSettingsCard preferences={desktopPreferences} onChange={setDesktop} />
      </div>
    </section>
  );
}
