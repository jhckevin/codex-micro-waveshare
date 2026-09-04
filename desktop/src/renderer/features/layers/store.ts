import {
  setActiveLayerCount,
  setKeyAction,
  setKeyIcon,
  type DeviceProfile,
  type EditableControlGroup,
  type IconRef,
  type KeyAction
} from "./model";

export interface SelectedKey {
  group: EditableControlGroup;
  index: number;
}

export interface LayerEditorState {
  profile: DeviceProfile;
  selectedLayer: number;
  selectedKey: SelectedKey | null;
}

export type LayerEditorAction =
  | { type: "replace-profile"; profile: DeviceProfile }
  | { type: "select-layer"; layer: number }
  | { type: "select-key"; key: SelectedKey }
  | { type: "set-layer-count"; count: number }
  | {
      type: "set-icon";
      layer: number;
      group: EditableControlGroup;
      index: number;
      icon: IconRef;
    }
  | {
      type: "set-action";
      layer: number;
      group: EditableControlGroup;
      index: number;
      action: KeyAction;
    };

export function layerEditorReducer(
  state: LayerEditorState,
  action: LayerEditorAction
): LayerEditorState {
  switch (action.type) {
    case "replace-profile":
      return {
        profile: action.profile,
        selectedLayer: 1,
        selectedKey: null
      };
    case "select-layer":
      return {
        ...state,
        selectedLayer: Math.max(1, Math.min(state.profile.activeLayerCount, action.layer)),
        selectedKey: null
      };
    case "select-key":
      return { ...state, selectedKey: action.key };
    case "set-layer-count": {
      const profile = setActiveLayerCount(state.profile, action.count);
      return {
        ...state,
        profile,
        selectedLayer: Math.min(state.selectedLayer, action.count)
      };
    }
    case "set-icon":
      return {
        ...state,
        profile: setKeyIcon(
          state.profile,
          action.layer,
          action.group,
          action.index,
          action.icon
        )
      };
    case "set-action":
      return {
        ...state,
        profile: setKeyAction(
          state.profile,
          action.layer,
          action.group,
          action.index,
          action.action
        )
      };
  }
}
