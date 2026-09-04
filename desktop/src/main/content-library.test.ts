import { mkdtemp, readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { describe, expect, it } from "vitest";

import { ContentLibraryRepository } from "./content-library";

const icon = (id: string, value: number) => ({
  id,
  alpha: new Uint8Array(48 * 48).fill(value)
});

describe("ContentLibraryRepository", () => {
  it("atomically persists and reloads device-ready A8 icons", async () => {
    const root = await mkdtemp(path.join(tmpdir(), "codex-content-"));
    const target = path.join(root, "content-library.json");
    const repository = new ContentLibraryRepository(target);

    expect(await repository.load()).toEqual([]);
    await repository.save([icon("custom-a", 0x7f)]);
    expect(await repository.load()).toEqual([icon("custom-a", 0x7f)]);
    expect(JSON.parse(await readFile(target, "utf8"))).toMatchObject({
      schema: 1,
      icons: [{ id: "custom-a" }]
    });
  });

  it("rejects invalid and duplicate libraries before writing", async () => {
    const root = await mkdtemp(path.join(tmpdir(), "codex-content-"));
    const repository = new ContentLibraryRepository(
      path.join(root, "content-library.json"));

    await expect(repository.save([
      icon("same", 1),
      icon("same", 2)
    ])).rejects.toThrow(/duplicate/i);
    await expect(repository.save([{
      id: "bad/path",
      alpha: new Uint8Array(48 * 48)
    }])).rejects.toThrow(/identifier/i);
  });
});
