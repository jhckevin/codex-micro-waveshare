import { createHash } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { mkdtemp, rm } from "node:fs/promises";
import { afterEach, describe, expect, it } from "vitest";

import { ProfileRepository } from "./profile-repository";


interface TestProfile {
  activeLayerCount: number;
  name: string;
}

const roots: string[] = [];

afterEach(async () => {
  await Promise.all(roots.splice(0).map((root) => rm(root, {
    recursive: true,
    force: true
  })));
});

async function repository() {
  const root = await mkdtemp(path.join(tmpdir(), "codex-profile-"));
  roots.push(root);
  const target = path.join(root, "profile.json");
  return {
    target,
    repository: new ProfileRepository<TestProfile>(
      target,
      (value): value is TestProfile =>
        typeof value === "object" &&
        value !== null &&
        Number.isInteger((value as TestProfile).activeLayerCount) &&
        (value as TestProfile).activeLayerCount >= 2 &&
        (value as TestProfile).activeLayerCount <= 6 &&
        typeof (value as TestProfile).name === "string"
    )
  };
}

describe("ProfileRepository", () => {
  it("returns an explicit fallback when no committed profile exists", async () => {
    const subject = await repository();
    const fallback = { activeLayerCount: 6, name: "Classical" };

    await expect(subject.repository.load(fallback)).resolves.toEqual({
      revision: 0,
      profile: fallback
    });
  });

  it("atomically saves and reloads schema-versioned profiles", async () => {
    const subject = await repository();
    const profile = { activeLayerCount: 3, name: "Work" };

    await expect(subject.repository.save(profile)).resolves.toEqual({
      revision: 1,
      profile
    });
    await expect(subject.repository.load({
      activeLayerCount: 6,
      name: "fallback"
    })).resolves.toEqual({ revision: 1, profile });

    const wire = JSON.parse(await readFile(subject.target, "utf8"));
    expect(wire.schema).toBe(1);
    expect(wire.revision).toBe(1);
    expect(wire.profile_sha256).toBe(
      createHash("sha256").update(JSON.stringify(profile)).digest("hex")
    );
  });

  it("increments the committed revision", async () => {
    const subject = await repository();
    await subject.repository.save({ activeLayerCount: 2, name: "First" });

    await expect(
      subject.repository.save({ activeLayerCount: 4, name: "Second" })
    ).resolves.toMatchObject({ revision: 2 });
  });

  it("rejects malformed or hash-mismatched committed data", async () => {
    const subject = await repository();
    await writeFile(subject.target, JSON.stringify({
      schema: 1,
      revision: 1,
      profile_sha256: "0".repeat(64),
      profile: { activeLayerCount: 3, name: "Tampered" }
    }));

    await expect(
      subject.repository.load({ activeLayerCount: 6, name: "fallback" })
    ).rejects.toThrow("profile integrity");
  });

  it("validates before replacing the previous commit", async () => {
    const subject = await repository();
    const original = { activeLayerCount: 2, name: "Original" };
    await subject.repository.save(original);

    await expect(subject.repository.save({
      activeLayerCount: 9,
      name: "Invalid"
    })).rejects.toThrow("profile validation");

    await expect(subject.repository.load(original)).resolves.toEqual({
      revision: 1,
      profile: original
    });
  });
});
