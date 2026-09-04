import { useEffect, useState } from "react";
import { Cable, RefreshCw, ShieldCheck } from "lucide-react";

type PackageClass =
  | "compatibility"
  | "service_reload"
  | "complete_firmware"
  | "user_content";

interface UpdateProgress {
  state?: string;
  expected_offset?: number;
  total_size?: number;
  target_version?: string;
}

interface AppUpdateStatus {
  phase: "idle" | "checking" | "up-to-date" | "available" |
    "downloading" | "downloaded" | "error";
  currentVersion: string;
  targetVersion: string | null;
  percent: number | null;
  error: string | null;
}

export function UpdatesPage() {
  const [status, setStatus] =
    useState("请使用原生 USB 端口连接设备。");
  const [packageClass, setPackageClass] =
    useState<PackageClass>("compatibility");
  const [busy, setBusy] = useState(false);
  const [awaitingConfirmation, setAwaitingConfirmation] = useState(false);
  const [progress, setProgress] = useState<UpdateProgress>();
  const [appUpdate, setAppUpdate] = useState<AppUpdateStatus>();

  useEffect(() => window.codexMicro?.onUpdateProgress((value) => {
    if (typeof value === "object" && value !== null) {
      setProgress(value as UpdateProgress);
    }
  }), []);
  useEffect(() => {
    void window.codexMicro?.getAppUpdateStatus().then((value) => {
      if (typeof value === "object" && value !== null) {
        setAppUpdate(value as AppUpdateStatus);
      }
    });
    return window.codexMicro?.onAppUpdateStatus((value) => {
      if (typeof value === "object" && value !== null) {
        setAppUpdate(value as AppUpdateStatus);
      }
    });
  }, []);

  async function runAppUpdate(action: "check" | "download" | "install") {
    if (!window.codexMicro) return;
    if (action === "check") {
      setAppUpdate(await window.codexMicro.checkAppUpdate() as AppUpdateStatus);
    } else if (action === "download") {
      setAppUpdate(await window.codexMicro.downloadAppUpdate() as AppUpdateStatus);
    } else {
      await window.codexMicro.installAppUpdate();
    }
  }

  const transferred = progress?.expected_offset ?? 0;
  const total = progress?.total_size ?? 0;
  const percent = total > 0
    ? Math.min(100, Math.round(transferred * 100 / total))
    : 0;

  async function inspect() {
    if (!window.codexMicro) return;
    setBusy(true);
    try {
      await window.codexMicro.connect("usb");
      const result = await window.codexMicro.inspectUpdates() as {
        provisioned?: boolean;
        device_id?: string;
      };
      setStatus(result.provisioned
        ? `设备已认证（${result.device_id?.slice(0, 8) ?? "unknown"}），可以接收设备专属加密更新。`
        : "设备尚未配置不可读硬件密钥；为保证安全，官方更新保持禁用。");
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy(false);
    }
  }

  async function install() {
    if (!window.codexMicro) return;
    setBusy(true);
    setProgress(undefined);
    setAwaitingConfirmation(false);
    setStatus("正在认证设备并获取设备专属密文……");
    try {
      const result =
        await window.codexMicro.installLatestUpdate(packageClass) as
          UpdateProgress;
      setProgress(result);
      setStatus(result.state === "awaiting_confirmation"
        ? `目标版本 ${result.target_version ?? ""} 已传输，等待确认。`
        : `更新状态：${result.state ?? "unknown"}`);
      setAwaitingConfirmation(result.state === "awaiting_confirmation");
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      setStatus(message.includes("verification_failed")
        ? "设备拒绝了此更新：它可能已安装，或签名、设备绑定、版本序列不匹配。"
        : message);
    } finally {
      setBusy(false);
    }
  }

  async function finish(confirm: boolean) {
    if (!window.codexMicro) return;
    setBusy(true);
    try {
      if (confirm) {
        await window.codexMicro.confirmUpdate(
          packageClass === "complete_firmware");
        setStatus(packageClass === "complete_firmware"
          ? "完整固件已验证，设备正在重启到目标版本。"
          : "更新已验证并激活。");
      } else {
        await window.codexMicro.cancelUpdate();
        setStatus("更新已取消，当前版本保持不变。");
      }
      setAwaitingConfirmation(false);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="page">
      <header className="page-header">
        <div>
          <p className="eyebrow">UPDATES</p>
          <h1>固件与兼容包</h1>
          <p>
            客户端只中继设备专属密文；协议兼容包、服务资源包和完整固件均只通过 USB 传输。
          </p>
        </div>
      </header>
      <div className="update-card">
        <div className="update-icon"><ShieldCheck size={24} /></div>
        <div>
          <h2>设备专属加密更新</h2>
          <p>{status}</p>
          {(busy || total > 0) && (
            <div className="update-progress" aria-label={`更新进度 ${percent}%`}>
              <span style={{ width: `${percent}%` }} />
              <small>{percent}%</small>
            </div>
          )}
          <div className="update-actions">
            <button disabled={busy} onClick={() => void inspect()}>
              检查设备
            </button>
            <select
              aria-label="更新类型"
              value={packageClass}
              disabled={busy}
              onChange={(event) =>
                setPackageClass(event.target.value as PackageClass)}
            >
              <option value="compatibility">协议兼容热补丁</option>
              <option value="service_reload">服务与资源包</option>
              <option value="complete_firmware">完整固件</option>
              <option value="user_content">签名键帽内容包</option>
            </select>
            <button disabled={busy} onClick={() => void install()}>
              {busy ? "处理中…" : "认证并更新"}
            </button>
            {awaitingConfirmation && (
              <>
                <button disabled={busy} onClick={() => void finish(true)}>
                  确认应用
                </button>
                <button disabled={busy} onClick={() => void finish(false)}>
                  取消
                </button>
              </>
            )}
          </div>
        </div>
        <span className="usb-only"><Cable size={15} /> USB ONLY</span>
      </div>
      <div className="update-card">
        <div className="update-icon"><RefreshCw size={24} /></div>
        <div>
          <h2>Codex Micro Control 客户端</h2>
          <p>
            当前版本 {appUpdate?.currentVersion ?? "—"}
            {appUpdate?.targetVersion ? ` · 可用版本 ${appUpdate.targetVersion}` : ""}
            {appUpdate?.error ? ` · ${appUpdate.error}` : ""}
          </p>
          {appUpdate?.phase === "downloading" && (
            <div className="update-progress" aria-label={`客户端下载进度 ${appUpdate.percent ?? 0}%`}>
              <span style={{ width: `${appUpdate.percent ?? 0}%` }} />
              <small>{appUpdate.percent ?? 0}%</small>
            </div>
          )}
          <div className="update-actions">
            <button disabled={appUpdate?.phase === "checking"}
              onClick={() => void runAppUpdate("check")}>检查客户端更新</button>
            {appUpdate?.phase === "available" && (
              <button onClick={() => void runAppUpdate("download")}>下载更新</button>
            )}
            {appUpdate?.phase === "downloaded" && (
              <button onClick={() => void runAppUpdate("install")}>重启并安装</button>
            )}
          </div>
        </div>
      </div>
    </section>
  );
}
