import { BatteryCharging, Layers3, RefreshCw, Settings, SlidersHorizontal } from "lucide-react";
import { Link, Route, Switch, useLocation } from "wouter";

import { useDevice } from "./device-context";
import { DevicePage } from "./pages/device";
import { LayersPage } from "./pages/layers";
import { SettingsPage } from "./pages/settings";
import { UpdatesPage } from "./pages/updates";

const navigation = [
  { to: "/", label: "设备", icon: SlidersHorizontal },
  { to: "/layers", label: "按键与层", icon: Layers3 },
  { to: "/settings", label: "设置", icon: Settings },
  { to: "/updates", label: "更新", icon: RefreshCw }
] as const;

function AppShell() {
  const { snapshot } = useDevice();
  const [location] = useLocation();
  const device = snapshot.device;
  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark" aria-hidden="true">
            <svg viewBox="0 0 20 20" fill="none">
              <path d="M13.33 11.42h-2.5a.665.665 0 0 0 0 1.33h2.5a.665.665 0 0 0 0-1.33ZM6.74 7.35a.665.665 0 0 1 .91.23L8.9 9.66a.66.66 0 0 1 0 .68l-1.25 2.09a.665.665 0 1 1-1.14-.69L7.56 10 6.51 8.26a.665.665 0 0 1 .23-.91Z" fill="currentColor"/>
              <path fillRule="evenodd" d="M9 1.75c1.12 0 2.14.4 2.92 1.07.27-.05.54-.07.81-.07a4.52 4.52 0 0 1 4.45 5.33 4.52 4.52 0 0 1-1.92 7.18 4.52 4.52 0 0 1-7.18 1.92 4.52 4.52 0 0 1-5.26-5.26A4.52 4.52 0 0 1 4.74 4.74 4.52 4.52 0 0 1 9 1.75Zm0 1.33a3.2 3.2 0 0 0-3.08 2.37.67.67 0 0 1-.47.47 3.19 3.19 0 0 0-1.26 5.34c.17.17.23.41.17.64a3.19 3.19 0 0 0 3.74 3.91.66.66 0 0 1 .64.18 3.19 3.19 0 0 0 5.35-1.43.66.66 0 0 1 .47-.47 3.19 3.19 0 0 0 1.43-5.35.66.66 0 0 1-.18-.64 3.19 3.19 0 0 0-3.91-3.74.66.66 0 0 1-.64-.17A3.18 3.18 0 0 0 9 3.08Z" fill="currentColor"/>
            </svg>
          </div>
          <div>
            <strong>Codex Micro Control</strong>
            <span>Developer Preview · Jhckevin</span>
          </div>
        </div>

        <nav>
          {navigation.map(({ to, label, icon: Icon }) => (
            <Link
              key={to}
              href={to}
              className={location === to ? "active" : ""}
            >
              <Icon size={18} strokeWidth={1.8} />
              {label}
            </Link>
          ))}
        </nav>

        <div className="sidebar-device">
          <div className="status-row">
            <span className={`status-dot ${snapshot.session.status === "connected" ? "connected" : ""}`} />
            <span>
              {snapshot.session.status === "connected"
                ? `${snapshot.session.transport.toUpperCase()} 已连接`
                : "等待设备"}
            </span>
          </div>
          <div className="battery-row">
            <BatteryCharging size={16} />
            <span>
              {device === null
                ? "连接后读取电量"
                : device.batteryPresent
                  ? `${device.batteryPercent}%${
                      device.batteryCharging
                        ? " · 充电中"
                        : device.batteryExternalPower
                          ? " · USB 已充满"
                          : ""
                    }`
                  : "未检测到电池"}
            </span>
          </div>
        </div>
      </aside>

      <main className="workspace">
        <Switch>
          <Route path="/"><DevicePage /></Route>
          <Route path="/layers"><LayersPage /></Route>
          <Route path="/settings"><SettingsPage /></Route>
          <Route path="/updates"><UpdatesPage /></Route>
          <Route><DevicePage /></Route>
        </Switch>
      </main>
    </div>
  );
}

export function AppRouter() {
  return <AppShell />;
}
