import type { AppControlEvent } from "../shared/protocol";


export type WindowsAction =
  | { kind: "unassigned" }
  | { kind: "chord"; keys: readonly string[] }
  | { kind: "pulse"; keys: readonly string[] }
  | { kind: "focus-app"; target: string };

export interface WindowsInputAdapter {
  keyDown(key: string): Promise<void>;
  keyUp(key: string): Promise<void>;
  focusOrLaunch(target: string): Promise<void>;
}

export type ActionResolver = (event: AppControlEvent) => WindowsAction;

export type WindowsPresetId =
  | "copy"
  | "paste"
  | "cut"
  | "windows-key"
  | "click-to-do"
  | "codex"
  | "chatgpt"
  | "always-on-top"
  | "typeless"
  | "typeless-hold"
  | "typeless-press"
  | "voice-typing"
  | "command-palette";

const WINDOWS_PRESETS: Readonly<Record<WindowsPresetId, WindowsAction>> = {
  copy: { kind: "chord", keys: ["Control", "C"] },
  paste: { kind: "chord", keys: ["Control", "V"] },
  cut: { kind: "chord", keys: ["Control", "X"] },
  "windows-key": { kind: "chord", keys: ["Windows"] },
  "click-to-do": { kind: "chord", keys: ["Windows", "Q"] },
  codex: { kind: "focus-app", target: "codex" },
  chatgpt: { kind: "focus-app", target: "chatgpt" },
  "always-on-top": {
    kind: "chord",
    keys: ["Windows", "Control", "T"]
  },
  typeless: { kind: "chord", keys: ["Alt", "Space"] },
  "typeless-hold": { kind: "chord", keys: ["RightAlt"] },
  "typeless-press": { kind: "pulse", keys: ["RightAlt"] },
  "voice-typing": { kind: "chord", keys: ["Windows", "H"] },
  "command-palette": {
    kind: "chord",
    keys: ["Windows", "Alt", "Space"]
  }
};

export function windowsPresetAction(preset: string): WindowsAction {
  if (!Object.prototype.hasOwnProperty.call(WINDOWS_PRESETS, preset)) {
    return { kind: "unassigned" };
  }
  const action = WINDOWS_PRESETS[preset as WindowsPresetId];
  if (action.kind === "chord" || action.kind === "pulse") {
    return {
      kind: action.kind,
      keys: [...action.keys]
    };
  }
  return { ...action };
}

function controlKey(event: AppControlEvent): string {
  return `${event.group}:${event.id}`;
}

export class ActionEngine {
  private readonly heldControls = new Map<string, readonly string[]>();
  private readonly keyReferences = new Map<string, number>();
  private readonly acquisitionOrder: string[] = [];
  private chain: Promise<void> = Promise.resolve();

  constructor(
    private readonly input: WindowsInputAdapter,
    private readonly resolveAction: ActionResolver
  ) {}

  get heldControlCount(): number {
    return this.heldControls.size;
  }

  handle(event: AppControlEvent): Promise<void> {
    const operation = this.chain.then(() => this.handleExclusive(event));
    this.chain = operation.catch(() => undefined);
    return operation;
  }

  releaseAll(): Promise<void> {
    const operation = this.chain.then(() => this.releaseAllExclusive());
    this.chain = operation.catch(() => undefined);
    return operation;
  }

  private async handleExclusive(event: AppControlEvent): Promise<void> {
    if (event.action === 0) {
      await this.releaseControl(controlKey(event));
      return;
    }
    const action = this.resolveAction(event);
    if (event.action === 1) {
      await this.pressControl(controlKey(event), action);
      return;
    }
    if (action.kind === "chord" || action.kind === "pulse") {
      await this.pulseChord(action.keys);
    } else if (action.kind === "focus-app") {
      await this.input.focusOrLaunch(action.target);
    }
  }

  private async pressControl(
    key: string,
    action: WindowsAction
  ): Promise<void> {
    if (this.heldControls.has(key)) return;
    if (action.kind === "unassigned") return;
    if (action.kind === "focus-app") {
      this.heldControls.set(key, []);
      try {
        await this.input.focusOrLaunch(action.target);
      } catch (error) {
        this.heldControls.delete(key);
        throw error;
      }
      return;
    }
    if (action.kind === "pulse") {
      // Keep an empty ownership marker until the physical release so a noisy
      // touch source cannot retrigger a toggle-style shortcut.
      this.heldControls.set(key, []);
      try {
        await this.pulseChord(action.keys);
      } catch (error) {
        this.heldControls.delete(key);
        throw error;
      }
      return;
    }
    const keys = this.normalizedChord(action.keys);
    if (keys.length === 0) return;
    await this.acquireChord(keys);
    this.heldControls.set(key, keys);
  }

  private async releaseControl(key: string): Promise<void> {
    const keys = this.heldControls.get(key);
    if (keys === undefined) return;
    this.heldControls.delete(key);
    for (let index = keys.length - 1; index >= 0; index -= 1) {
      await this.releaseKey(keys[index]);
    }
  }

  private async pulseChord(rawKeys: readonly string[]): Promise<void> {
    const keys = this.normalizedChord(rawKeys);
    if (keys.length === 0) return;
    await this.acquireChord(keys);
    for (let index = keys.length - 1; index >= 0; index -= 1) {
      await this.releaseKey(keys[index]);
    }
  }

  private normalizedChord(keys: readonly string[]): readonly string[] {
    const result: string[] = [];
    const seen = new Set<string>();
    for (const key of keys) {
      if (typeof key !== "string" || key.length === 0 || seen.has(key)) continue;
      seen.add(key);
      result.push(key);
    }
    return result;
  }

  private async acquireChord(keys: readonly string[]): Promise<void> {
    const acquired: string[] = [];
    try {
      for (const key of keys) {
        const references = this.keyReferences.get(key) ?? 0;
        if (references === 0) {
          await this.input.keyDown(key);
          this.acquisitionOrder.push(key);
        }
        this.keyReferences.set(key, references + 1);
        acquired.push(key);
      }
    } catch (error) {
      for (let index = acquired.length - 1; index >= 0; index -= 1) {
        try {
          await this.releaseKey(acquired[index]);
        } catch {
          // Keep the original injection failure as the actionable error.
        }
      }
      throw error;
    }
  }

  private async releaseKey(key: string): Promise<void> {
    const references = this.keyReferences.get(key) ?? 0;
    if (references === 0) return;
    if (references > 1) {
      this.keyReferences.set(key, references - 1);
      return;
    }
    await this.input.keyUp(key);
    this.keyReferences.delete(key);
    const orderIndex = this.acquisitionOrder.lastIndexOf(key);
    if (orderIndex >= 0) this.acquisitionOrder.splice(orderIndex, 1);
  }

  private async releaseAllExclusive(): Promise<void> {
    this.heldControls.clear();
    const keys = [...this.acquisitionOrder].reverse();
    let firstError: unknown;
    for (const key of keys) {
      try {
        await this.input.keyUp(key);
      } catch (error) {
        firstError ??= error;
      }
    }
    this.keyReferences.clear();
    this.acquisitionOrder.length = 0;
    if (firstError !== undefined) throw firstError;
  }
}

export interface ActionEventSource {
  onEvent(listener: (event: AppControlEvent) => void): () => void;
}

export interface ActionStatusSource {
  subscribe(listener: (snapshot: { status: string }) => void): () => void;
}

export class DeviceActionBridge {
  private readonly removeEventListener: () => void;
  private readonly removeStatusListener: () => void;
  private chain: Promise<void> = Promise.resolve();
  private closed = false;

  constructor(
    events: ActionEventSource,
    status: ActionStatusSource,
    private readonly engine: ActionEngine,
    private readonly reportError: (error: Error) => void = () => undefined
  ) {
    this.removeEventListener = events.onEvent((event) => {
      this.enqueue(() => this.engine.handle(event));
    });
    this.removeStatusListener = status.subscribe((snapshot) => {
      if (snapshot.status === "disconnected") {
        this.enqueue(() => this.engine.releaseAll());
      }
    });
  }

  flush(): Promise<void> {
    return this.chain;
  }

  async close(): Promise<void> {
    if (this.closed) {
      await this.flush();
      return;
    }
    this.closed = true;
    this.removeEventListener();
    this.removeStatusListener();
    this.enqueue(() => this.engine.releaseAll(), true);
    await this.flush();
  }

  private enqueue(operation: () => Promise<void>, allowClosed = false): void {
    if (this.closed && !allowClosed) return;
    this.chain = this.chain
      .then(operation)
      .catch((error: unknown) => {
        this.reportError(
          error instanceof Error ? error : new Error(String(error))
        );
      });
  }
}
