import { createHash } from "node:crypto";

import type { DeviceTransport } from "./device-session";

export interface ContentProgress {
  state: string;
  expected_offset: number;
  total_size: number;
}

const CHUNK_SIZE = 512;
const MAXIMUM_BYTES = 2 * 1024 * 1024;

export class DeviceContentSession {
  constructor(private readonly transport: DeviceTransport) {}

  async inspect(): Promise<ContentProgress> {
    this.requireUsb();
    return decodeProgress(await this.request({ method: "content.status" }));
  }

  async install(
    pack: Uint8Array,
    onProgress: (progress: ContentProgress) => void = () => undefined
  ): Promise<ContentProgress> {
    this.requireUsb();
    validatePackEnvelope(pack);
    const digest = createHash("sha256").update(pack).digest("hex");
    let progress = decodeProgress(await this.request({
      method: "content.begin",
      params: {
        total_size: pack.byteLength,
        sha256: digest
      }
    }));
    if (progress.total_size !== pack.byteLength ||
        progress.expected_offset > pack.byteLength) {
      throw new Error("Device content resume state does not match this pack");
    }
    for (let offset = progress.expected_offset;
         offset < pack.byteLength;
         offset += CHUNK_SIZE) {
      const chunk = pack.subarray(
        offset, Math.min(offset + CHUNK_SIZE, pack.byteLength));
      progress = decodeProgress(await this.request({
        method: "content.chunk",
        params: {
          offset,
          data: Buffer.from(chunk).toString("hex")
        }
      }));
      onProgress(progress);
    }
    progress = decodeProgress(await this.request({ method: "content.commit" }));
    onProgress(progress);
    return progress;
  }

  async cancel(): Promise<ContentProgress> {
    this.requireUsb();
    return decodeProgress(await this.request({ method: "content.cancel" }));
  }

  private requireUsb(): void {
    if (this.transport.kind !== "usb") {
      throw new Error("Custom keycap content requires native USB");
    }
  }

  private async request(message: {
    method: string;
    params?: Record<string, unknown>;
  }): Promise<Record<string, unknown>> {
    const reply = await this.transport.request(message);
    if (typeof reply !== "object" || reply === null) {
      throw new Error("Malformed content response");
    }
    const value = reply as Record<string, unknown>;
    if (value.ok !== true) {
      throw new Error(String(value.error ?? "Content transfer rejected"));
    }
    return value;
  }
}

function validatePackEnvelope(pack: Uint8Array): void {
  if (!(pack instanceof Uint8Array) ||
      pack.byteLength < 16 || pack.byteLength > MAXIMUM_BYTES ||
      new TextDecoder().decode(pack.subarray(0, 4)) !== "CMC1" ||
      pack[4] !== 1 || pack[6] !== 48 || pack[7] !== 48) {
    throw new Error("Invalid device-ready CMC1 content pack");
  }
  const totalSize = new DataView(
    pack.buffer, pack.byteOffset, pack.byteLength).getUint32(8, true);
  if (totalSize !== pack.byteLength) {
    throw new Error("Invalid device-ready CMC1 content pack");
  }
}

function decodeProgress(value: Record<string, unknown>): ContentProgress {
  if (typeof value.state !== "string" ||
      !Number.isInteger(value.expected_offset) ||
      !Number.isInteger(value.total_size)) {
    throw new Error("Malformed content progress");
  }
  return {
    state: value.state,
    expected_offset: Number(value.expected_offset),
    total_size: Number(value.total_size)
  };
}
