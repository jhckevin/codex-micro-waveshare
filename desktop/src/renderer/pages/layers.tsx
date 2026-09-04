import { Save } from "lucide-react";
import { useEffect, useReducer, useState } from "react";

import { DevicePreview } from "../features/layers/device-preview";
import {
  createDefaultProfile,
  type DeviceProfile
} from "../features/layers/model";
import { RouteEditor } from "../features/layers/route-editor";
import { layerEditorReducer } from "../features/layers/store";

export function LayersPage() {
  const [state, dispatch] = useReducer(layerEditorReducer, {
    profile: createDefaultProfile(),
    selectedLayer: 1,
    selectedKey: null
  });
  const [saveState, setSaveState] = useState<"idle" | "saving" | "saved" | "offline">("idle");
  useEffect(() => {
    let active = true;
    void window.codexMicro?.loadProfile().then((snapshot) => {
      if (!active || typeof snapshot !== "object" || snapshot === null ||
          !("profile" in snapshot)) return;
      dispatch({
        type: "replace-profile",
        profile: (snapshot as { profile: DeviceProfile }).profile
      });
    }).catch(() => {
      if (active) setSaveState("offline");
    });
    return () => {
      active = false;
    };
  }, []);
  const layer = state.profile.layers[state.selectedLayer - 1];
  return (
    <section className="page">
      <header className="page-header">
        <div>
          <p className="eyebrow">KEYS & LAYERS</p>
          <h1>按键、键帽与路由</h1>
          <p>每层图标与动作相互独立；Layer 1 默认保持 Codex Classical。</p>
        </div>
      </header>
      <div className="layer-toolbar">
        <div className="layer-tabs">
          {Array.from({ length: state.profile.activeLayerCount }, (_, index) => {
            const number = index + 1;
            return (
              <button
                key={number}
                type="button"
                className={state.selectedLayer === number ? "active" : ""}
                onClick={() => dispatch({ type: "select-layer", layer: number })}
              >
                <span>L{number}</span>
                <small>{number === 1 ? "Codex Classical" : `Layer ${number}`}</small>
              </button>
            );
          })}
        </div>
        <label className="layer-count">
          循环层数
          <select
            value={state.profile.activeLayerCount}
            onChange={(event) =>
              dispatch({ type: "set-layer-count", count: Number(event.target.value) })
            }
          >
            {[2, 3, 4, 5, 6].map((count) => <option key={count}>{count}</option>)}
          </select>
        </label>
        <button
          type="button"
          className="save-routing"
          disabled={saveState === "saving"}
          onClick={async () => {
            if (!window.codexMicro) {
              setSaveState("offline");
              return;
            }
            setSaveState("saving");
            try {
              await window.codexMicro.saveProfile(state.profile);
              setSaveState("saved");
            } catch {
              setSaveState("offline");
            }
          }}
        >
          <Save size={14} />
          {saveState === "saving"
            ? "写入中"
            : saveState === "saved"
              ? "已写入设备"
              : saveState === "offline"
                ? "请先连接"
                : "写入设备"}
        </button>
      </div>

      <div className="layer-editor-layout">
        <div className="preview-stage">
          <div className="preview-stage-label">
            <span>实时布局预览</span>
            <small>{layer.name}</small>
          </div>
          <DevicePreview
            profile={state.profile}
            layer={layer}
            selectedKey={state.selectedKey}
            onSelect={(key) => dispatch({ type: "select-key", key })}
          />
          <p className="routing-note">
            客户端断开后，固件会关闭路由租约并立即恢复 Codex 默认行为。
          </p>
        </div>
        <RouteEditor
          profile={state.profile}
          layer={state.selectedLayer}
          selectedKey={state.selectedKey}
          onIcon={(icon) => {
            if (!state.selectedKey) return;
            dispatch({
              type: "set-icon",
              layer: state.selectedLayer,
              ...state.selectedKey,
              icon
            });
          }}
          onAction={(action) => {
            if (!state.selectedKey) return;
            dispatch({
              type: "set-action",
              layer: state.selectedLayer,
              ...state.selectedKey,
              action
            });
          }}
          onImportIcon={async (icon) => {
            if (!window.codexMicro) {
              throw new Error("请先通过原生 USB 连接设备");
            }
            await window.codexMicro.installCustomIcon(icon);
          }}
        />
      </div>
    </section>
  );
}
