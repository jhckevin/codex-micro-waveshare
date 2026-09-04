import { createHash } from "node:crypto";

import type { DeviceTransport } from "./device-session";

export interface EncryptedUpdateChunk {
  offset: number;
  sequence: number;
  ciphertext_hex: string;
  tag_hex: string;
  plaintext_sha256_hex: string;
}

export interface EncryptedUpdatePackage {
  target_version: string;
  estimated_seconds: number;
  package_class:
    | "compatibility"
    | "service_reload"
    | "complete_firmware"
    | "user_content";
  manifest_hex: string;
  chunks: readonly EncryptedUpdateChunk[];
}

export interface UpdateProgress {
  state: string;
  expected_offset: number;
  next_sequence: number;
  total_size: number;
  target_version: string;
}

export class DeviceUpdateRelay {
  constructor(private readonly transport: DeviceTransport) {}

  async inspect(): Promise<Record<string, unknown>> {
    this.requireUsb();
    return this.request({ method: "update.hello" });
  }

  async attest(challengeHex: string): Promise<{
    device_id: string;
    tag: string;
  }> {
    this.requireUsb();
    if (!/^[0-9a-f]{64}$/i.test(challengeHex)) {
      throw new Error("Invalid update challenge");
    }
    const reply = await this.request({
      method: "update.attest",
      params: { challenge: challengeHex }
    });
    if (
      typeof reply.device_id !== "string" ||
      !/^[0-9a-f]{32}$/i.test(reply.device_id) ||
      typeof reply.tag !== "string" ||
      !/^[0-9a-f]{64}$/i.test(reply.tag)
    ) {
      throw new Error("Malformed device attestation");
    }
    return { device_id: reply.device_id, tag: reply.tag };
  }

  async install(
    updatePackage: EncryptedUpdatePackage,
    onProgress: (progress: UpdateProgress) => void = () => undefined
  ): Promise<UpdateProgress> {
    this.requireUsb();
    validateCiphertextOnlyPackage(updatePackage);
    let progress = decodeProgress(await this.request({ method: "update.status" }));
    const payloadBytes = updatePackage.chunks.reduce(
      (total, chunk) => total + chunk.ciphertext_hex.length / 2, 0);
    const resumable = progress.state === "receiving" &&
      progress.total_size === payloadBytes &&
      progress.target_version === updatePackage.target_version;
    if (!resumable) {
      progress = decodeProgress(await this.request({
        method: "update.begin",
        params: { manifest: updatePackage.manifest_hex }
      }));
    }
    for (const chunk of updatePackage.chunks) {
      if (chunk.offset < progress.expected_offset) continue;
      if (
        chunk.offset !== progress.expected_offset ||
        chunk.sequence !== progress.next_sequence
      ) {
        throw new Error("Update package does not match device resume offset");
      }
      progress = decodeProgress(await this.request({
        method: "update.chunk",
        params: {
          offset: chunk.offset,
          sequence: chunk.sequence,
          data: chunk.ciphertext_hex,
          tag: chunk.tag_hex,
          sha256: chunk.plaintext_sha256_hex
        }
      }));
      onProgress(progress);
    }
    progress = decodeProgress(await this.request({ method: "update.commit" }));
    onProgress(progress);
    return progress;
  }

  async confirm(): Promise<UpdateProgress> {
    this.requireUsb();
    return decodeProgress(await this.request({ method: "update.confirm" }));
  }

  async cancel(): Promise<UpdateProgress> {
    this.requireUsb();
    return decodeProgress(await this.request({ method: "update.cancel" }));
  }

  private requireUsb(): void {
    if (this.transport.kind !== "usb") {
      throw new Error("Firmware, compatibility, and keycap content require USB");
    }
  }

  private async request(message: { method: string; params?: Record<string, unknown> }) {
    const reply = await this.transport.request(message);
    if (typeof reply !== "object" || reply === null || !("ok" in reply)) {
      throw new Error("Malformed update response");
    }
    const value = reply as Record<string, unknown>;
    if (value.ok !== true) throw new Error(String(value.error ?? "Update rejected"));
    return value;
  }
}

function decodeProgress(value: Record<string, unknown>): UpdateProgress {
  if (
    typeof value.state !== "string" ||
    !Number.isInteger(value.expected_offset) ||
    !Number.isInteger(value.next_sequence) ||
    !Number.isInteger(value.total_size)
  ) {
    throw new Error("Malformed update progress");
  }
  return {
    state: value.state,
    expected_offset: Number(value.expected_offset),
    next_sequence: Number(value.next_sequence),
    total_size: Number(value.total_size),
    target_version:
      typeof value.target_version === "string" ? value.target_version : ""
  };
}

export function validateCiphertextOnlyPackage(
  updatePackage: EncryptedUpdatePackage
): void {
  if (!/^[0-9a-f]{496}$/i.test(updatePackage.manifest_hex)) {
    throw new Error("Invalid signed manifest");
  }
  for (const chunk of updatePackage.chunks) {
    if (
      !Number.isInteger(chunk.offset) ||
      !Number.isInteger(chunk.sequence) ||
      !/^[0-9a-f]{2,1024}$/i.test(chunk.ciphertext_hex) ||
      chunk.ciphertext_hex.length % 2 !== 0 ||
      !/^[0-9a-f]{32}$/i.test(chunk.tag_hex) ||
      !/^[0-9a-f]{64}$/i.test(chunk.plaintext_sha256_hex)
    ) {
      throw new Error("Invalid encrypted update chunk");
    }
    // Force parsing in the untrusted relay, without ever materializing
    // firmware plaintext.
    createHash("sha256").update(Buffer.from(chunk.ciphertext_hex, "hex")).digest();
  }
}
