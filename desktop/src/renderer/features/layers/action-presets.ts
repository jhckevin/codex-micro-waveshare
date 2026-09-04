import type { ShortcutPresetId } from "./model";

export interface ActionPreset {
  id: ShortcutPresetId;
  label: string;
  description: string;
  windows: readonly string[];
  icon: string;
}

export const ACTION_PRESETS: readonly ActionPreset[] = [
  { id: "copy", label: "复制", description: "复制所选内容", windows: ["Ctrl", "C"], icon: "copy" },
  { id: "paste", label: "粘贴", description: "粘贴剪贴板内容", windows: ["Ctrl", "V"], icon: "clipboard" },
  { id: "cut", label: "剪切", description: "剪切所选内容", windows: ["Ctrl", "X"], icon: "scissors" },
  { id: "windows-key", label: "Windows 键", description: "打开开始菜单", windows: ["Win"], icon: "windows" },
  { id: "click-to-do", label: "Click to Do", description: "Copilot+ PC 快捷入口", windows: ["Win", "Q"], icon: "mouse-pointer" },
  { id: "codex", label: "打开 Codex", description: "将 Codex 切换到前台", windows: [], icon: "codex" },
  { id: "chatgpt", label: "打开 ChatGPT", description: "将 ChatGPT 切换到前台", windows: [], icon: "message" },
  { id: "always-on-top", label: "窗口置顶", description: "PowerToys Always On Top", windows: ["Win", "Ctrl", "T"], icon: "pin" },
  { id: "typeless", label: "Typeless", description: "触发 Typeless", windows: ["Alt", "Space"], icon: "type" },
  { id: "typeless-hold", label: "Typeless 按住说话", description: "按下和松开分别发送右 Alt", windows: ["Right Alt"], icon: "microphone" },
  { id: "typeless-press", label: "Typeless 点按切换", description: "按下时发送一次右 Alt", windows: ["Right Alt"], icon: "microphone" },
  { id: "voice-typing", label: "微软语音输入", description: "打开系统语音输入", windows: ["Win", "H"], icon: "microphone" },
  { id: "command-palette", label: "命令面板", description: "打开 Windows Command Palette", windows: ["Win", "Alt", "Space"], icon: "terminal" }
];

export function getActionPreset(id: ShortcutPresetId): ActionPreset {
  const preset = ACTION_PRESETS.find((candidate) => candidate.id === id);
  if (!preset) throw new RangeError(`Unknown action preset: ${id}`);
  return preset;
}
