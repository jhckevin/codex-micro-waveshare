import { mkdtemp, readFile, readdir, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";

import {
  releaseSmokeOutput,
  writeReleaseSmokeMarker
} from "./release-smoke";


const temporaryDirectories: string[] = [];

afterEach(async () => {
  await Promise.all(temporaryDirectories.splice(0).map(
    (directory) => rm(directory, { recursive: true, force: true })
  ));
});

describe("release smoke marker", () => {
  it("accepts exactly one absolute nonce marker", async () => {
    const directory = await mkdtemp(path.join(os.tmpdir(), "codex-smoke-"));
    temporaryDirectories.push(directory);
    const output = path.join(
      directory,
      "codex-micro-smoke-0123456789abcdef0123456789abcdef.json"
    );

    expect(releaseSmokeOutput([
      "Codex Micro Control.exe",
      `--release-smoke-output=${output}`
    ])).toBe(path.resolve(output));
  });

  it("rejects relative duplicate and non-nonce paths", () => {
    expect(() => releaseSmokeOutput([
      "--release-smoke-output=smoke.json"
    ])).toThrow("absolute nonce marker");
    expect(() => releaseSmokeOutput([
      "--release-smoke-output=/tmp/codex-micro-smoke-nope.json"
    ])).toThrow("absolute nonce marker");
    expect(() => releaseSmokeOutput([
      "--release-smoke-output=/tmp/codex-micro-smoke-0123456789abcdef0123456789abcdef.json",
      "--release-smoke-output=/tmp/codex-micro-smoke-fedcba9876543210fedcba9876543210.json"
    ])).toThrow("Malformed release smoke");
  });

  it("creates but never overwrites a bounded marker", async () => {
    const directory = await mkdtemp(path.join(os.tmpdir(), "codex-smoke-"));
    temporaryDirectories.push(directory);
    const output = path.join(
      directory,
      "codex-micro-smoke-0123456789abcdef0123456789abcdef.json"
    );
    const marker = {
      schema: 1 as const,
      packaged: true,
      platform: "win32",
      arch: "x64",
      package_version: "1.0.1",
      renderer_loaded: true as const
    };

    await writeReleaseSmokeMarker(output, marker);
    expect(JSON.parse(await readFile(output, "utf8"))).toEqual(marker);
    expect((await readdir(directory)).filter(
      (name) => name.endsWith(".tmp")
    )).toEqual([]);
    await expect(
      writeReleaseSmokeMarker(output, marker)
    ).rejects.toThrow();
  });

  it("rejects malformed marker metadata", async () => {
    const directory = await mkdtemp(path.join(os.tmpdir(), "codex-smoke-"));
    temporaryDirectories.push(directory);
    const output = path.join(
      directory,
      "codex-micro-smoke-0123456789abcdef0123456789abcdef.json"
    );
    await expect(writeReleaseSmokeMarker(output, {
      schema: 1,
      packaged: true,
      platform: "win32",
      arch: "x64",
      package_version: "1.0.1.1",
      renderer_loaded: true
    })).rejects.toThrow("Invalid release smoke marker");
  });
});
