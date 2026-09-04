import type { ProfileSnapshot } from "./profile-repository";
import {
  ProfileRuntime,
  createDefaultRuntimeProfile,
  isRuntimeDeviceProfile,
  toRuntimeRoutingConfig,
  type RuntimeDeviceProfile
} from "./profile-runtime";
import type { DeviceRequest } from "../shared/protocol";

export interface ProfileStorage {
  load(
    fallback: RuntimeDeviceProfile
  ): Promise<ProfileSnapshot<RuntimeDeviceProfile>>;
  save(
    profile: RuntimeDeviceProfile
  ): Promise<ProfileSnapshot<RuntimeDeviceProfile>>;
}

export interface RoutingSession {
  request(message: DeviceRequest): Promise<unknown>;
}

const DEVICE_BUILTIN_IDS: Readonly<Record<string, string>> = {
  bolt: "FAST",
  check: "APPR",
  close: "REJ",
  "computer-use": "COMPUTER",
  microphone: "MIC",
  codex: "OAI"
};

function resolveIconId(
  profile: RuntimeDeviceProfile,
  layer: number,
  group: "agent" | "command",
  index: number,
  visited = new Set<string>()
): string {
  const location = `${layer}:${group}:${index}`;
  if (visited.has(location)) return "__blank";
  visited.add(location);
  const icon = profile.layers[layer - 1]?.keys[group][index]?.icon;
  if (icon === undefined || icon.kind === "blank") return "__blank";
  if (icon.kind === "custom") return icon.id;
  if (icon.kind === "builtin") {
    return DEVICE_BUILTIN_IDS[icon.id] ?? icon.id.toUpperCase();
  }
  if (icon.group !== "agent" && icon.group !== "command") return "__blank";
  return resolveIconId(
    profile, icon.layer, icon.group, icon.index, visited
  );
}

function iconMessages(profile: RuntimeDeviceProfile): DeviceRequest[] {
  const messages: DeviceRequest[] = [];
  for (let layer = 1; layer <= 6; layer += 1) {
    for (const group of ["agent", "command"] as const) {
      messages.push({
        method: "icons.set",
        params: {
          layer,
          group,
          ids: Array.from({ length: 6 }, (_, index) =>
            resolveIconId(profile, layer, group, index)
          )
        }
      });
    }
  }
  return messages;
}

export class ProfileController {
  readonly runtime = new ProfileRuntime();
  private current: ProfileSnapshot<RuntimeDeviceProfile> = {
    revision: 0,
    profile: createDefaultRuntimeProfile()
  };

  constructor(private readonly storage: ProfileStorage) {}

  get snapshot(): ProfileSnapshot<RuntimeDeviceProfile> {
    return {
      revision: this.current.revision,
      profile: this.runtime.profile
    };
  }

  async initialize(): Promise<ProfileSnapshot<RuntimeDeviceProfile>> {
    const loaded = await this.storage.load(createDefaultRuntimeProfile());
    this.runtime.replace(loaded.profile);
    this.current = {
      revision: loaded.revision,
      profile: this.runtime.profile
    };
    return this.snapshot;
  }

  async save(
    profile: unknown,
    session?: RoutingSession
  ): Promise<ProfileSnapshot<RuntimeDeviceProfile>> {
    if (!isRuntimeDeviceProfile(profile)) {
      throw new TypeError("profile validation failed");
    }
    const saved = await this.storage.save(profile);
    this.runtime.replace(saved.profile);
    this.current = {
      revision: saved.revision,
      profile: this.runtime.profile
    };
    if (session !== undefined) await this.apply(session);
    return this.snapshot;
  }

  async apply(session: RoutingSession): Promise<void> {
    const routing = toRuntimeRoutingConfig(this.runtime.profile);
    const reply = await session.request({
      method: "routing.set",
      params: { ...routing }
    });
    if (
      typeof reply !== "object" ||
      reply === null ||
      (reply as { ok?: unknown }).ok !== true
    ) {
      throw new Error("Device rejected routing profile");
    }
    for (const message of iconMessages(this.runtime.profile)) {
      const iconReply = await session.request(message);
      if (
        typeof iconReply !== "object" ||
        iconReply === null ||
        (iconReply as { ok?: unknown }).ok !== true
      ) {
        throw new Error("Device rejected icon profile");
      }
    }
  }
}
