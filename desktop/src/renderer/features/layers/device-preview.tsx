import {
  Bot,
  Check,
  CircleX,
  Cloud,
  Code2,
  Expand,
  Mic,
  MousePointer2,
  Sparkles,
  Star
} from "lucide-react";
import type { ComponentType } from "react";

import type {
  DeviceProfile,
  EditableControlGroup,
  IconRef,
  LayerDefinition
} from "./model";
import type { SelectedKey } from "./store";

const iconMap: Record<string, ComponentType<{ size?: number; strokeWidth?: number }>> = {
  bolt: Sparkles,
  check: Check,
  close: CircleX,
  "computer-use": MousePointer2,
  microphone: Mic,
  codex: Code2,
  favourite: Star,
  cloud: Cloud
};

function resolveIcon(
  profile: DeviceProfile,
  layer: LayerDefinition,
  group: EditableControlGroup,
  index: number,
  icon: IconRef
): IconRef {
  if (icon.kind !== "inherit") return icon;
  return profile.layers[icon.layer - 1].keys[icon.group][icon.index].icon;
}

function KeyGlyph({
  icon,
  agent
}: {
  icon: IconRef;
  agent?: boolean;
}) {
  if (icon.kind === "blank") return null;
  if (agent) return <span className="agent-plus">+</span>;
  const Icon = icon.kind === "builtin" ? iconMap[icon.id] ?? Bot : Bot;
  return <Icon size={23} strokeWidth={1.9} />;
}

interface KeyButtonProps {
  profile: DeviceProfile;
  layer: LayerDefinition;
  group: EditableControlGroup;
  index: number;
  className?: string;
  selected: boolean;
  onSelect(key: SelectedKey): void;
}

function KeyButton({
  profile,
  layer,
  group,
  index,
  className = "",
  selected,
  onSelect
}: KeyButtonProps) {
  const key = layer.keys[group][index];
  const icon = resolveIcon(profile, layer, group, index, key.icon);
  const privateId = (layer.number - 1) * 6 + Math.min(index, 5);
  return (
    <button
      type="button"
      className={`editor-key ${group} ${className} ${selected ? "selected" : ""}`}
      onClick={() => onSelect({ group, index })}
      aria-label={`${group} ${index + 1}`}
    >
      <KeyGlyph icon={icon} agent={group === "agent"} />
      {layer.number > 1 && key.action.kind !== "codex" && (
        <small>{String(privateId).padStart(2, "0")}</small>
      )}
    </button>
  );
}

export interface DevicePreviewProps {
  profile: DeviceProfile;
  layer: LayerDefinition;
  selectedKey: SelectedKey | null;
  onSelect(key: SelectedKey): void;
}

export function DevicePreview({
  profile,
  layer,
  selectedKey,
  onSelect
}: DevicePreviewProps) {
  const selected = (group: EditableControlGroup, index: number) =>
    selectedKey?.group === group && selectedKey.index === index;
  return (
    <div className="editor-device-wrap">
      <div className="editor-ambient" />
      <div className="editor-device">
        <button
          type="button"
          className={`editor-knob ${selected("encoder", 0) ? "selected" : ""}`}
          onClick={() => onSelect({ group: "encoder", index: 0 })}
          aria-label="encoder"
        />
        <KeyButton profile={profile} layer={layer} group="agent" index={0} selected={selected("agent", 0)} onSelect={onSelect} />
        <KeyButton profile={profile} layer={layer} group="agent" index={1} selected={selected("agent", 1)} onSelect={onSelect} />
        <button
          type="button"
          className={`editor-stick ${selected("joystick", 0) ? "selected" : ""}`}
          onClick={() => onSelect({ group: "joystick", index: 0 })}
          aria-label="joystick"
        />
        {[2, 3, 4, 5].map((index) => (
          <KeyButton key={`agent-${index}`} profile={profile} layer={layer} group="agent" index={index} selected={selected("agent", index)} onSelect={onSelect} />
        ))}
        {[0, 1, 2, 3].map((index) => (
          <KeyButton key={`command-${index}`} profile={profile} layer={layer} group="command" index={index} selected={selected("command", index)} onSelect={onSelect} />
        ))}
        <div className="editor-layer-lights">
          <i className={layer.number === 1 || layer.number >= 4 ? "on" : ""} />
          <i className={[2, 4, 5, 6].includes(layer.number) ? "on" : ""} />
          <i className={[3, 5, 6].includes(layer.number) ? "on" : ""} />
        </div>
        <KeyButton profile={profile} layer={layer} group="command" index={4} className="wide" selected={selected("command", 4)} onSelect={onSelect} />
        <KeyButton profile={profile} layer={layer} group="command" index={5} selected={selected("command", 5)} onSelect={onSelect} />
      </div>
    </div>
  );
}
