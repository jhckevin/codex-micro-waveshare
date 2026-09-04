import { Cable, Check, Image, Keyboard, Link2, LockKeyhole } from "lucide-react";
import { useState } from "react";

import { ACTION_PRESETS } from "./action-presets";
import type { DeviceProfile, EditableControlGroup, IconRef, KeyAction } from "./model";
import type { SelectedKey } from "./store";
import { rasterizeMonochromeSvg } from "./svg-icon";

const iconChoices = [
  { id: "bolt", label: "闪电" },
  { id: "check", label: "确认" },
  { id: "close", label: "关闭" },
  { id: "computer-use", label: "Computer Use" },
  { id: "favourite", label: "Favourite" },
  { id: "codex", label: "Codex" },
  { id: "microphone", label: "语音" },
  { id: "message-circle-plus", label: "新消息" }
] as const;

function actionLabel(action: KeyAction): string {
  if (action.kind === "codex") return "Codex 原生";
  if (action.kind === "unassigned") return "未映射";
  if (action.kind === "shortcut") {
    return ACTION_PRESETS.find((preset) => preset.id === action.preset)?.label ?? action.preset;
  }
  if (action.kind === "custom-chord") return action.keys.join(" + ");
  if (action.kind === "custom-pulse") return `点按：${action.keys.join(" + ")}`;
  return `打开 ${action.target}`;
}

const CUSTOM_KEYS = new Set([
  "Alt", "RightAlt", "Control", "Shift", "Windows", "Space", "Enter",
  "Escape", "Tab", "Backspace", "Delete", "Insert", "Home", "End",
  "PageUp", "PageDown", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
  ...Array.from({ length: 26 }, (_, index) =>
    String.fromCharCode("A".charCodeAt(0) + index)),
  ...Array.from({ length: 10 }, (_, index) => String(index)),
  ...Array.from({ length: 24 }, (_, index) => `F${index + 1}`)
]);

function CustomActionEditor({
  action,
  onAction
}: {
  action: KeyAction;
  onAction(action: KeyAction): void;
}) {
  const [chord, setChord] = useState(
    action.kind === "custom-chord" || action.kind === "custom-pulse"
      ? action.keys.join(" + ")
      : ""
  );
  const [target, setTarget] = useState(
    action.kind === "launch-app" ? action.target : ""
  );
  const [error, setError] = useState("");
  const applyKeys = (kind: "custom-chord" | "custom-pulse") => {
    const keys = chord.split("+").map((key) => key.trim()).filter(Boolean);
    if (keys.length < 1 || keys.length > 8 ||
        keys.some((key) => !CUSTOM_KEYS.has(key))) {
      setError("请使用 1–8 个受支持的 Windows 键名。");
      return;
    }
    setError("");
    onAction({ kind, keys });
  };
  return (
    <div className="custom-action-editor">
      <label>
        自定义按键或组合键
        <input
          value={chord}
          placeholder="例如 Control + Shift + P"
          onChange={(event) => setChord(event.target.value)}
        />
      </label>
      <button
        type="button"
        onClick={() => applyKeys("custom-chord")}
      >
        应用按住映射
      </button>
      <button
        type="button"
        onClick={() => applyKeys("custom-pulse")}
      >
        应用点按映射
      </button>
      <label>
        打开指定应用
        <input
          value={target}
          placeholder="C:\\Program Files\\App\\App.exe"
          onChange={(event) => setTarget(event.target.value)}
        />
      </label>
      <button
        type="button"
        onClick={() => {
          if (!/^[A-Za-z]:\\.+\.exe$/i.test(target) ||
              target.includes("\\..\\")) {
            setError("请输入本机绝对 .exe 路径。");
            return;
          }
          setError("");
          onAction({ kind: "launch-app", target });
        }}
      >
        应用程序映射
      </button>
      {error && <small className="custom-action-error">{error}</small>}
    </div>
  );
}

export interface RouteEditorProps {
  profile: DeviceProfile;
  layer: number;
  selectedKey: SelectedKey | null;
  onIcon(icon: IconRef): void;
  onAction(action: KeyAction): void;
  onImportIcon(icon: { id: string; alpha: Uint8Array }): Promise<void>;
}

export function RouteEditor({
  profile,
  layer,
  selectedKey,
  onIcon,
  onAction,
  onImportIcon
}: RouteEditorProps) {
  const [importStatus, setImportStatus] = useState("");
  if (!selectedKey) {
    return (
      <aside className="route-editor no-selection">
        <Link2 size={25} />
        <h3>选择一个按键</h3>
        <p>键帽图标与按键动作分别保存，修改其中一个不会影响另一个。</p>
      </aside>
    );
  }

  const key = profile.layers[layer - 1].keys[selectedKey.group][selectedKey.index];
  const locked = layer === 1 && selectedKey.group === "agent";
  const isMicrophone =
    selectedKey.group === "command" && selectedKey.index === 4;
  const title = isMicrophone
    ? `MIC · Layer ${layer}`
    : `${selectedKey.group.toUpperCase()} ${String(
        selectedKey.index + 1).padStart(2, "0")}`;
  return (
    <aside className="route-editor">
      <div className="editor-panel-heading">
        <div>
          <span>当前按键</span>
          <h3>{title}</h3>
        </div>
        {locked && <LockKeyhole size={17} />}
      </div>

      <div className="editor-section">
        <label><Image size={15} /> 键帽图标</label>
        <p>所有 SVG 都会被转换为单色黑色。</p>
        <div className="icon-choice-grid">
          {iconChoices.map((choice) => {
            const active = key.icon.kind === "builtin" && key.icon.id === choice.id;
            return (
              <button
                key={choice.id}
                type="button"
                className={active ? "active" : ""}
                onClick={() => onIcon({ kind: "builtin", id: choice.id })}
              >
                {active && <Check size={12} />}
                {choice.label}
              </button>
            );
          })}
          <button type="button" onClick={() => onIcon({ kind: "blank" })}>空白键帽</button>
          {layer > 1 && (
            <button
              type="button"
              onClick={() =>
                onIcon({
                  kind: "inherit",
                  layer: 1,
                  group: selectedKey.group as EditableControlGroup,
                  index: selectedKey.index
                })
              }
            >
              跟随 Layer 1
            </button>
          )}
        </div>
        <label className="usb-content-button">
          <Cable size={14} />
          通过 USB 导入单色 SVG
          <input
            hidden
            type="file"
            accept=".svg,image/svg+xml"
            onChange={(event) => {
              const file = event.target.files?.[0];
              event.target.value = "";
              if (!file) return;
              setImportStatus("正在安全转换并写入设备…");
              void file.text()
                .then(rasterizeMonochromeSvg)
                .then(async (alpha) => {
                  const digestInput = Uint8Array.from(alpha);
                  const digest = new Uint8Array(
                    await crypto.subtle.digest("SHA-256", digestInput.buffer));
                  const suffix = Array.from(digest.subarray(0, 8))
                    .map((value) => value.toString(16).padStart(2, "0"))
                    .join("");
                  const id = `custom-${suffix}`;
                  await onImportIcon({ id, alpha });
                  onIcon({ kind: "custom", id });
                  setImportStatus("已通过 USB 写入并应用");
                })
                .catch((error: unknown) => {
                  setImportStatus(
                    error instanceof Error ? error.message : String(error));
                });
            }}
          />
        </label>
        {importStatus && <small className="custom-import-status">{importStatus}</small>}
      </div>

      <div className="editor-section">
        <label><Keyboard size={15} /> 按键动作</label>
        <p>当前：{actionLabel(key.action)}</p>
        {locked ? (
          <div className="locked-note">Layer 1 的 Agent 键始终交给 Codex。</div>
        ) : (
          <div className="action-list">
            <button type="button" onClick={() => onAction({ kind: "codex" })}>
              <strong>Codex 原生</strong><small>将该键透传到 Codex 通道</small>
            </button>
            {ACTION_PRESETS.map((preset) => (
              <button
                key={preset.id}
                type="button"
                className={
                  key.action.kind === "shortcut" && key.action.preset === preset.id
                    ? "active"
                    : ""
                }
                onClick={() => onAction({ kind: "shortcut", preset: preset.id })}
              >
                <strong>{preset.label}</strong>
                <small>{preset.windows.join(" + ") || preset.description}</small>
              </button>
            ))}
            <CustomActionEditor
              key={`${layer}-${selectedKey.group}-${selectedKey.index}`}
              action={key.action}
              onAction={onAction}
            />
          </div>
        )}
      </div>
    </aside>
  );
}
