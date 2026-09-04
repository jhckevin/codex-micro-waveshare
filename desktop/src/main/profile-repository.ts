import { createHash, randomUUID } from "node:crypto";
import {
  mkdir,
  open,
  readFile,
  rename,
  rm
} from "node:fs/promises";
import path from "node:path";


interface StoredProfile {
  schema: 1;
  revision: number;
  profile_sha256: string;
  profile: unknown;
}

export interface ProfileSnapshot<T> {
  revision: number;
  profile: T;
}

type ProfileValidator<T> = (value: unknown) => value is T;

function profileHash(profile: unknown): string {
  return createHash("sha256")
    .update(JSON.stringify(profile))
    .digest("hex");
}

function isMissing(error: unknown): boolean {
  return (
    typeof error === "object" &&
    error !== null &&
    "code" in error &&
    (error as { code?: unknown }).code === "ENOENT"
  );
}

export class ProfileRepository<T> {
  constructor(
    private readonly target: string,
    private readonly validate: ProfileValidator<T>
  ) {}

  async load(fallback: T): Promise<ProfileSnapshot<T>> {
    const committed = await this.readCommitted();
    return committed ?? { revision: 0, profile: fallback };
  }

  async save(profile: T): Promise<ProfileSnapshot<T>> {
    if (!this.validate(profile)) {
      throw new TypeError("profile validation failed");
    }
    let canonical: string;
    let isolated: unknown;
    try {
      canonical = JSON.stringify(profile);
      isolated = JSON.parse(canonical);
    } catch {
      throw new TypeError("profile validation failed");
    }
    if (!this.validate(isolated)) {
      throw new TypeError("profile validation failed");
    }
    const previous = await this.readCommitted();
    const revision = (previous?.revision ?? 0) + 1;
    const envelope: StoredProfile = {
      schema: 1,
      revision,
      profile_sha256: createHash("sha256").update(canonical).digest("hex"),
      profile: isolated
    };
    const parent = path.dirname(this.target);
    await mkdir(parent, { recursive: true });
    const temporary = `${this.target}.${process.pid}.${randomUUID()}.tmp`;
    let handle: Awaited<ReturnType<typeof open>> | undefined;
    try {
      handle = await open(temporary, "wx", 0o600);
      await handle.writeFile(`${JSON.stringify(envelope)}\n`, "utf8");
      await handle.sync();
      await handle.close();
      handle = undefined;
      await rename(temporary, this.target);
    } catch (error) {
      if (handle !== undefined) {
        try {
          await handle.close();
        } catch {
          // Preserve the original filesystem error.
        }
      }
      await rm(temporary, { force: true }).catch(() => undefined);
      throw error;
    }
    return { revision, profile: isolated };
  }

  private async readCommitted(): Promise<ProfileSnapshot<T> | undefined> {
    let text: string;
    try {
      text = await readFile(this.target, "utf8");
    } catch (error) {
      if (isMissing(error)) return undefined;
      throw error;
    }
    let stored: unknown;
    try {
      stored = JSON.parse(text);
    } catch {
      throw new Error("profile integrity check failed");
    }
    if (
      typeof stored !== "object" ||
      stored === null ||
      Array.isArray(stored) ||
      (stored as Partial<StoredProfile>).schema !== 1 ||
      !Number.isSafeInteger((stored as Partial<StoredProfile>).revision) ||
      Number((stored as Partial<StoredProfile>).revision) < 1 ||
      typeof (stored as Partial<StoredProfile>).profile_sha256 !== "string" ||
      !this.validate((stored as Partial<StoredProfile>).profile)
    ) {
      throw new Error("profile integrity check failed");
    }
    const envelope = stored as StoredProfile;
    if (profileHash(envelope.profile) !== envelope.profile_sha256) {
      throw new Error("profile integrity check failed");
    }
    return {
      revision: envelope.revision,
      profile: envelope.profile as T
    };
  }
}
