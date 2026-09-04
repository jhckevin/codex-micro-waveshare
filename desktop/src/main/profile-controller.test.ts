import { describe, expect, it } from "vitest";

import {
  ProfileController,
  type ProfileStorage,
  type RoutingSession
} from "./profile-controller";
import {
  createDefaultRuntimeProfile,
  type RuntimeDeviceProfile
} from "./profile-runtime";

class MemoryStorage implements ProfileStorage {
  saved: RuntimeDeviceProfile[] = [];
  constructor(private loaded?: RuntimeDeviceProfile) {}

  async load(fallback: RuntimeDeviceProfile) {
    return { revision: this.loaded ? 3 : 0, profile: this.loaded ?? fallback };
  }

  async save(profile: RuntimeDeviceProfile) {
    this.saved.push(profile);
    this.loaded = profile;
    return { revision: this.saved.length + 3, profile };
  }
}

class RecordingSession implements RoutingSession {
  requests: unknown[] = [];
  failure?: Error;

  async request(message: { method: string; params?: Record<string, unknown> }) {
    this.requests.push(message);
    if (this.failure) throw this.failure;
    return { ok: true };
  }
}

describe("ProfileController", () => {
  it("loads the durable profile into the action runtime", async () => {
    const profile = createDefaultRuntimeProfile();
    profile.layers[1].keys.command[0].action = {
      kind: "shortcut",
      preset: "copy"
    };
    const controller = new ProfileController(new MemoryStorage(profile));

    const snapshot = await controller.initialize();

    expect(snapshot.revision).toBe(3);
    expect(controller.runtime.resolve({
      type: "control",
      group: "command",
      id: 6,
      action: 1
    })).toEqual({ kind: "chord", keys: ["Control", "C"] });
  });

  it("persists, activates, and applies one matching routing profile", async () => {
    const storage = new MemoryStorage();
    const session = new RecordingSession();
    const controller = new ProfileController(storage);
    await controller.initialize();
    const profile = createDefaultRuntimeProfile();
    profile.layers[0].keys.command[4].action = {
      kind: "shortcut",
      preset: "typeless-hold"
    };

    await controller.save(profile, session);

    expect(storage.saved).toHaveLength(1);
    expect(controller.runtime.resolve({
      type: "control",
      group: "command",
      id: 40,
      action: 1
    })).toEqual({ kind: "chord", keys: ["RightAlt"] });
    expect(session.requests[0]).toEqual({
      method: "routing.set",
      params: expect.objectContaining({
        enabled: true,
        layer_count: 6,
        layer1_command_targets: [0, 0, 0, 0, 7, 0]
      })
    });
    expect(session.requests).toHaveLength(13);
    expect(session.requests[1]).toEqual({
      method: "icons.set",
      params: {
        layer: 1,
        group: "agent",
        ids: [
          "AGENT-1", "AGENT-2", "AGENT-3",
          "AGENT-4", "AGENT-5", "AGENT-6"
        ]
      }
    });
    expect(session.requests[2]).toEqual({
      method: "icons.set",
      params: {
        layer: 1,
        group: "command",
        ids: ["FAST", "APPR", "REJ", "COMPUTER", "MIC", "OAI"]
      }
    });
    expect(session.requests[4]).toEqual(
      expect.objectContaining({
        params: expect.objectContaining({ layer: 2, group: "command" })
      })
    );
  });

  it("resolves inherited, custom and blank icon references before sending", async () => {
    const profile = createDefaultRuntimeProfile();
    profile.layers[0].keys.command[4].icon = { kind: "custom", id: "voice-mark" };
    profile.layers[1].keys.command[0].icon = { kind: "blank" };
    const controller = new ProfileController(new MemoryStorage(profile));
    const session = new RecordingSession();
    await controller.initialize();
    await controller.apply(session);

    expect(session.requests[2]).toEqual(
      expect.objectContaining({
        params: expect.objectContaining({
          ids: ["FAST", "APPR", "REJ", "COMPUTER", "voice-mark", "OAI"]
        })
      })
    );
    expect(session.requests[4]).toEqual(
      expect.objectContaining({
        params: expect.objectContaining({
          ids: ["__blank", "APPR", "REJ", "COMPUTER", "voice-mark", "OAI"]
        })
      })
    );
  });

  it("keeps the durable profile when a currently connected device rejects routing", async () => {
    const storage = new MemoryStorage();
    const session = new RecordingSession();
    session.failure = new Error("link lost");
    const controller = new ProfileController(storage);
    await controller.initialize();
    const profile = createDefaultRuntimeProfile();
    profile.layers[1].keys.command[0].action = {
      kind: "shortcut",
      preset: "paste"
    };

    await expect(controller.save(profile, session)).rejects.toThrow("link lost");

    expect(storage.saved).toHaveLength(1);
    expect(controller.runtime.resolve({
      type: "control",
      group: "command",
      id: 6,
      action: 1
    })).toEqual({ kind: "chord", keys: ["Control", "V"] });
  });
});
