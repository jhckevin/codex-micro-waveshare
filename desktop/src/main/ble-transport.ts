import { spawn as nodeSpawn } from "node:child_process";
import path from "node:path";
import { createInterface } from "node:readline";
import type { Readable, Writable } from "node:stream";

import type { DeviceTransport } from "./device-session";
import {
  decodeAppControlEvent,
  type AppControlEvent,
  type DeviceRequest
} from "../shared/protocol";

export const CODEX_CONFIG_SERVICE_UUID =
  "01584443-4f52-3891-a145-b6572a98f431";
export const CODEX_CONFIG_RX_UUID =
  "02584443-4f52-3891-a145-b6572a98f431";
export const CODEX_CONFIG_TX_UUID =
  "03584443-4f52-3891-a145-b6572a98f431";

const MAX_RECEIVE_TEXT = 8192;
const MAX_HELPER_ERROR = 240;

type NativeBleCommand =
  | {
      op: "open";
      service_uuid: string;
      rx_uuid: string;
      tx_uuid: string;
      require_encryption: true;
    }
  | { op: "write"; value_base64: string }
  | { op: "close" };

interface PendingReply {
  resolve(): void;
  reject(error: Error): void;
}

export interface NativeBleProcess {
  stdin: Writable;
  stdout: Readable;
  stderr: Readable;
  kill(): boolean;
  once?(
    event: "exit" | "error",
    listener: (...args: unknown[]) => void
  ): unknown;
}

export type NativeBleSpawn = (
  executable: string,
  args: readonly string[],
  options: {
    shell: false;
    windowsHide: true;
    stdio: ["pipe", "pipe", "pipe"];
  }
) => NativeBleProcess;

export interface BleByteChannel {
  write(data: Buffer): Promise<void>;
  onData(listener: (data: Buffer) => void): () => void;
  onError(listener: (error: Error) => void): () => void;
  close(): void | Promise<void>;
}

function isLocalAbsoluteHelper(
  helperPath: string,
  expectedDirectory: string
): boolean {
  if (
    !/^[A-Za-z]:\\/.test(helperPath) ||
    helperPath.startsWith("\\\\") ||
    /[\u0000-\u001f]/.test(helperPath) ||
    path.win32.basename(helperPath).toLowerCase() !== "codex-ble-helper.exe"
  ) return false;
  const actual = path.win32.resolve(path.win32.dirname(helperPath));
  const expected = path.win32.resolve(expectedDirectory);
  return actual.toLowerCase() === expected.toLowerCase();
}

function defaultSpawn(
  executable: string,
  args: readonly string[],
  options: {
    shell: false;
    windowsHide: true;
    stdio: ["pipe", "pipe", "pipe"];
  }
): NativeBleProcess {
  return nodeSpawn(executable, [...args], options);
}

export class NativeBleProcessChannel implements BleByteChannel {
  private readonly pending = new Map<number, PendingReply>();
  private readonly dataListeners = new Set<(data: Buffer) => void>();
  private readonly errorListeners = new Set<(error: Error) => void>();
  private nextId = 1;
  private closed = false;

  private constructor(private readonly process: NativeBleProcess) {
    const lines = createInterface({ input: process.stdout });
    lines.on("line", (line) => this.receive(line));
    process.once?.("exit", () =>
      this.fail(new Error("BLE helper exited"))
    );
    process.once?.("error", () =>
      this.fail(new Error("BLE helper failed to start"))
    );
  }

  static async open(
    helperPath: string,
    spawn: NativeBleSpawn = defaultSpawn,
    expectedDirectory = path.win32.dirname(helperPath)
  ): Promise<NativeBleProcessChannel> {
    if (!isLocalAbsoluteHelper(helperPath, expectedDirectory)) {
      throw new TypeError("Invalid BLE helper path");
    }
    const child = spawn(helperPath, [], {
      shell: false,
      windowsHide: true,
      stdio: ["pipe", "pipe", "pipe"]
    });
    const channel = new NativeBleProcessChannel(child);
    try {
      await channel.request({
        op: "open",
        service_uuid: CODEX_CONFIG_SERVICE_UUID,
        rx_uuid: CODEX_CONFIG_RX_UUID,
        tx_uuid: CODEX_CONFIG_TX_UUID,
        require_encryption: true
      });
      return channel;
    } catch (error) {
      channel.close();
      throw error;
    }
  }

  write(data: Buffer): Promise<void> {
    return this.request({
      op: "write",
      value_base64: data.toString("base64")
    });
  }

  onData(listener: (data: Buffer) => void): () => void {
    this.dataListeners.add(listener);
    return () => this.dataListeners.delete(listener);
  }

  onError(listener: (error: Error) => void): () => void {
    this.errorListeners.add(listener);
    return () => this.errorListeners.delete(listener);
  }

  close(): void {
    if (this.closed) {
      this.process.kill();
      return;
    }
    this.closed = true;
    const line = `${JSON.stringify({
      id: this.nextId,
      op: "close"
    })}\n`;
    this.process.stdin.write(line);
    this.process.kill();
    const error = new Error("BLE helper is closed");
    for (const pending of this.pending.values()) pending.reject(error);
    this.pending.clear();
    this.dataListeners.clear();
    this.errorListeners.clear();
  }

  private request(command: NativeBleCommand): Promise<void> {
    if (this.closed) {
      return Promise.reject(new Error("BLE helper is closed"));
    }
    const id = this.nextId;
    this.nextId = this.nextId === 0x7fffffff ? 1 : this.nextId + 1;
    return new Promise<void>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.process.stdin.write(
        `${JSON.stringify({ id, ...command })}\n`,
        (error) => {
          if (error === null || error === undefined) return;
          this.pending.delete(id);
          reject(new Error("BLE helper write failed"));
        }
      );
    });
  }

  private receive(line: string): void {
    let value: unknown;
    try {
      value = JSON.parse(line);
    } catch {
      this.fail(new Error("Malformed BLE helper message"));
      return;
    }
    if (typeof value !== "object" || value === null || Array.isArray(value)) {
      this.fail(new Error("Malformed BLE helper message"));
      return;
    }
    const message = value as Record<string, unknown>;
    if (message.event === "notification") {
      if (typeof message.value_base64 !== "string") {
        this.fail(new Error("Malformed BLE notification"));
        return;
      }
      let data: Buffer;
      try {
        data = Buffer.from(message.value_base64, "base64");
      } catch {
        this.fail(new Error("Malformed BLE notification"));
        return;
      }
      for (const listener of this.dataListeners) listener(data);
      return;
    }
    if (message.event === "closed") {
      const detail =
        typeof message.error === "string" && message.error.length > 0
          ? `: ${message.error.slice(0, MAX_HELPER_ERROR)}`
          : "";
      this.fail(new Error(`BLE link closed${detail}`));
      return;
    }
    if (
      !Number.isSafeInteger(message.id) ||
      typeof message.ok !== "boolean"
    ) {
      this.fail(new Error("Malformed BLE helper reply"));
      return;
    }
    const id = message.id as number;
    const pending = this.pending.get(id);
    if (pending === undefined) {
      this.fail(new Error("Unexpected BLE helper reply"));
      return;
    }
    this.pending.delete(id);
    if (message.ok) {
      pending.resolve();
      return;
    }
    const detail =
      typeof message.error === "string" && message.error.length > 0
        ? message.error.slice(0, MAX_HELPER_ERROR)
        : "unknown error";
    pending.reject(new Error(`BLE helper failed: ${detail}`));
  }

  private fail(error: Error): void {
    if (this.closed) return;
    this.closed = true;
    for (const pending of this.pending.values()) pending.reject(error);
    this.pending.clear();
    for (const listener of this.errorListeners) listener(error);
  }
}

export class CodexBleTransport implements DeviceTransport {
  readonly kind = "ble" as const;
  private receiveText = "";
  private pending?: {
    resolve(value: unknown): void;
    reject(error: Error): void;
    timer: NodeJS.Timeout;
  };
  private chain: Promise<unknown> = Promise.resolve();
  private closed = false;
  private readonly eventListeners = new Set<(event: AppControlEvent) => void>();
  private readonly removeDataListener: () => void;
  private readonly removeErrorListener: () => void;

  constructor(private readonly channel: BleByteChannel) {
    this.removeDataListener = channel.onData((data) => this.onData(data));
    this.removeErrorListener = channel.onError((error) =>
      this.failPending(error)
    );
  }

  static async open(
    helperPath: string,
    spawn?: NativeBleSpawn,
    expectedDirectory?: string
  ): Promise<CodexBleTransport> {
    return new CodexBleTransport(
      await NativeBleProcessChannel.open(
        helperPath,
        spawn,
        expectedDirectory
      )
    );
  }

  request(message: DeviceRequest): Promise<unknown> {
    if (this.closed) {
      return Promise.reject(new Error("BLE transport is closed"));
    }
    const operation = this.chain.then(() => this.requestExclusive(message));
    this.chain = operation.catch(() => undefined);
    return operation;
  }

  onEvent(listener: (event: AppControlEvent) => void): () => void {
    this.eventListeners.add(listener);
    return () => this.eventListeners.delete(listener);
  }

  async close(): Promise<void> {
    if (this.closed) return;
    this.closed = true;
    this.failPending(new Error("BLE transport closed"));
    this.removeDataListener();
    this.removeErrorListener();
    this.eventListeners.clear();
    await this.channel.close();
  }

  private async requestExclusive(message: DeviceRequest): Promise<unknown> {
    if (this.pending) throw new Error("Concurrent BLE request");
    const encoded = Buffer.from(`${JSON.stringify(message)}\n`, "utf8");
    const response = new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = undefined;
        reject(new Error("Device response timed out"));
      }, 5000);
      this.pending = { resolve, reject, timer };
    });
    try {
      await this.channel.write(encoded);
    } catch (error) {
      this.failPending(
        error instanceof Error ? error : new Error(String(error))
      );
    }
    return response;
  }

  private onData(data: Buffer): void {
    this.receiveText += data.toString("utf8");
    if (this.receiveText.length > MAX_RECEIVE_TEXT) {
      this.receiveText = "";
      this.failPending(new Error("Device response exceeded receive limit"));
      return;
    }
    let newline = this.receiveText.indexOf("\n");
    while (newline >= 0) {
      const line = this.receiveText.slice(0, newline);
      this.receiveText = this.receiveText.slice(newline + 1);
      if (line.length > 0) this.dispatchLine(line);
      newline = this.receiveText.indexOf("\n");
    }
  }

  private dispatchLine(line: string): void {
    let parsed: unknown;
    try {
      parsed = JSON.parse(line);
    } catch {
      this.failPending(new Error("Malformed JSON from device"));
      return;
    }
    if (
      typeof parsed === "object" &&
      parsed !== null &&
      !Array.isArray(parsed) &&
      (parsed as Record<string, unknown>).event === "input"
    ) {
      try {
        const event = decodeAppControlEvent(parsed);
        for (const listener of this.eventListeners) listener(event);
      } catch {
        // Invalid private input cannot consume or corrupt an RPC reply.
      }
      return;
    }
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = undefined;
    clearTimeout(pending.timer);
    pending.resolve(parsed);
  }

  private failPending(error: Error): void {
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = undefined;
    clearTimeout(pending.timer);
    pending.reject(error);
  }
}
