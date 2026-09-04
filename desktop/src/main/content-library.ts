import { mkdir, readFile, rename, unlink, writeFile } from "node:fs/promises";
import path from "node:path";

import {
  compileUserContentPack,
  type UserContentIcon
} from "./content-compiler";

interface StoredLibrary {
  schema: 1;
  icons: Array<{ id: string; alpha_base64: string }>;
}

export class ContentLibraryRepository {
  constructor(private readonly filePath: string) {}

  async load(): Promise<UserContentIcon[]> {
    let raw: string;
    try {
      raw = await readFile(this.filePath, "utf8");
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === "ENOENT") return [];
      throw error;
    }
    const parsed = JSON.parse(raw) as unknown;
    if (!isStoredLibrary(parsed)) {
      throw new Error("Invalid custom keycap library");
    }
    const icons = parsed.icons.map((icon) => ({
      id: icon.id,
      alpha: new Uint8Array(Buffer.from(icon.alpha_base64, "base64"))
    }));
    compileUserContentPack(icons);
    return cloneIcons(icons);
  }

  async save(icons: readonly UserContentIcon[]): Promise<UserContentIcon[]> {
    compileUserContentPack(icons);
    const canonical = [...icons]
      .sort((left, right) => left.id.localeCompare(right.id, "en"))
      .map((icon) => ({
        id: icon.id,
        alpha_base64: Buffer.from(icon.alpha).toString("base64")
      }));
    const stored: StoredLibrary = { schema: 1, icons: canonical };
    await mkdir(path.dirname(this.filePath), { recursive: true });
    const temporary =
      `${this.filePath}.${process.pid}.${Date.now().toString(36)}.tmp`;
    try {
      await writeFile(temporary, `${JSON.stringify(stored)}\n`, {
        encoding: "utf8",
        mode: 0o600
      });
      await rename(temporary, this.filePath);
    } catch (error) {
      await unlink(temporary).catch(() => undefined);
      throw error;
    }
    return cloneIcons(icons);
  }
}

function isStoredLibrary(value: unknown): value is StoredLibrary {
  if (typeof value !== "object" || value === null ||
      (value as { schema?: unknown }).schema !== 1 ||
      !Array.isArray((value as { icons?: unknown }).icons)) {
    return false;
  }
  return (value as StoredLibrary).icons.every((icon) => {
    if (typeof icon !== "object" || icon === null ||
        typeof icon.id !== "string" ||
        typeof icon.alpha_base64 !== "string" ||
        !/^[A-Za-z0-9+/]{3072}$/.test(icon.alpha_base64)) {
      return false;
    }
    return Buffer.from(icon.alpha_base64, "base64").byteLength === 48 * 48;
  });
}

function cloneIcons(
  icons: readonly UserContentIcon[]
): UserContentIcon[] {
  return icons.map((icon) => ({
    id: icon.id,
    alpha: new Uint8Array(icon.alpha)
  }));
}
