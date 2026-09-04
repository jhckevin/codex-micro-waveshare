import {
  Battery,
  BatteryCharging,
  Bluetooth,
  Cable,
  ChevronRight,
  RefreshCw,
  Unplug
} from "lucide-react";

import { useDevice } from "../device-context";
import type { DeviceTransportKind } from "../../shared/protocol";


export function DevicePage() {
  const { store, snapshot } = useDevice();
  const connected = snapshot.session.status === "connected";
  const device = snapshot.device;

  const connect = (kind: DeviceTransportKind) => {
    void store.connect(kind).catch(() => undefined);
  };

  return (
    <section className="page">
      <header className="page-header">
        <div>
          <p className="eyebrow">DEVICE</p>
          <h1>你的 Codex Micro</h1>
          <p>独立 App 通道负责设置与多层配置，Codex 通道和外环状态灯保持解耦。</p>
        </div>
        <button
          className="primary-button"
          disabled={snapshot.busy}
          onClick={() => {
            if (connected) void store.disconnect().catch(() => undefined);
            else connect("usb");
          }}
          type="button"
        >
          {snapshot.busy ? (
            <RefreshCw className="spin" size={17} />
          ) : connected ? (
            <Unplug size={17} />
          ) : (
            <Cable size={17} />
          )}
          {connected ? "断开设备" : "通过 USB 连接"}
        </button>
      </header>

      {snapshot.error && (
        <div className="inline-alert" role="alert">{snapshot.error}</div>
      )}

      <div className="device-hero">
        <div className="device-visual" aria-label="Codex Micro preview">
          <div className="ambient-rail" />
          <div className="device-face">
            <div className="preview-key knob" />
            <div className="preview-key agent blue"><span>+</span></div>
            <div className="preview-key agent green"><span>+</span></div>
            <div className="preview-key joystick" />
            <div className="preview-key agent faint"><span>+</span></div>
            <div className="preview-key agent amber"><span>+</span></div>
            <div className="preview-key agent rose"><span>+</span></div>
            <div className="preview-key agent faint"><span>+</span></div>
            <div className="preview-key command">⌁</div>
            <div className="preview-key command">✓</div>
            <div className="preview-key command">×</div>
            <div className="preview-key command">↗</div>
            <div className="preview-key touch" />
            <div className="preview-key command wide">◉</div>
            <div className="preview-key command">◌</div>
          </div>
        </div>

        <div className="connection-panel">
          <div>
            <p className="eyebrow">CONNECTION</p>
            <h2>{connected ? "设备已连接" : "尚未连接"}</h2>
            <p>
              {connected
                ? `${snapshot.session.transport.toUpperCase()} · Layer ${device?.layer ?? "—"}`
                : "日常配置支持 USB 与 BLE；图标包和固件更新仅允许 USB。"}
            </p>
          </div>

          {device && (
            <div className="battery-card">
              <span className="connection-icon">
                {device.batteryCharging
                  ? <BatteryCharging size={20} />
                  : <Battery size={20} />}
              </span>
              <div>
                <strong>
                  {device.batteryPresent
                    ? `${device.batteryPercent}%`
                    : "未检测到电池"}
                </strong>
                <small>
                  {device.batteryPresent
                    ? `${device.batteryCharging
                        ? "正在充电"
                        : device.batteryExternalPower
                          ? "USB 供电 · 已充满"
                          : "电池供电"} · ${device.batteryVoltageMv} mV${
                        device.batteryCharging
                          ? ` · 配置上限 ${device.chargeLimitMa} mA / ${(device.chargePowerLimitMw / 1000).toFixed(2)} W`
                          : ""
                      }`
                    : "对 Codex / Windows 上报 100%，设备端保持 no battery"}
                </small>
              </div>
            </div>
          )}

          <div className="connection-options">
            <button
              className={snapshot.session.transport === "usb" && connected ? "selected" : ""}
              disabled={snapshot.busy}
              onClick={() => connect("usb")}
              type="button"
            >
              <span className="connection-icon"><Cable size={19} /></span>
              <span><strong>USB</strong><small>推荐 · 完整功能与更新</small></span>
              <ChevronRight size={17} />
            </button>
            <button
              className={snapshot.session.transport === "ble" && connected ? "selected" : ""}
              disabled={snapshot.busy}
              onClick={() => connect("ble")}
              type="button"
            >
              <span className="connection-icon"><Bluetooth size={19} /></span>
              <span><strong>Bluetooth LE</strong><small>日常设置，不传输固件与图标包</small></span>
              <ChevronRight size={17} />
            </button>
          </div>
        </div>
      </div>
    </section>
  );
}
