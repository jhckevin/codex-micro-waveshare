import { spawn } from "node:child_process";
import path from "node:path";
import { createInterface } from "node:readline";

import type { DeviceTransport } from "./device-session";
import {
  decodeAppControlEvent,
  type AppControlEvent,
  type DeviceRequest
} from "../shared/protocol";

export class CodexUsbCdcTransport implements DeviceTransport {
  readonly kind = "usb" as const;
  private pending?: {
    resolve(value: unknown): void;
    reject(error: Error): void;
    timer: NodeJS.Timeout;
  };
  private chain: Promise<unknown> = Promise.resolve();
  private closed = false;
  private readonly listeners = new Set<(event: AppControlEvent) => void>();
  private readonly ready: Promise<void>;
  private readyResolve?: () => void;
  private readyReject?: (error: Error) => void;

  private constructor(private readonly child: ReturnType<typeof spawn>) {
    this.ready = new Promise<void>((resolve, reject) => {
      this.readyResolve = resolve;
      this.readyReject = reject;
    });
    createInterface({ input: child.stdout! }).on("line", (line) =>
      this.receive(line)
    );
    child.once("error", () => this.fail(new Error("USB CDC helper failed")));
    child.once("exit", () => this.fail(new Error("USB CDC helper exited")));
  }

  static async open(helperPath: string): Promise<CodexUsbCdcTransport> {
    const resolved = path.win32.resolve(helperPath);
    if (
      !/^[A-Za-z]:\\/.test(resolved) ||
      path.win32.basename(resolved).toLowerCase() !== "codex-usb-cdc-helper.ps1"
    ) throw new TypeError("Invalid USB CDC helper path");
    const child = spawn(
      "powershell.exe",
      ["-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", resolved],
      { shell: false, windowsHide: true, stdio: ["pipe", "pipe", "pipe"] }
    );
    const transport = new CodexUsbCdcTransport(child);
    await Promise.race([
      transport.ready,
      new Promise<never>((_, reject) => setTimeout(
        () => reject(new Error("USB CDC helper timed out")), 5000
      ))
    ]);
    return transport;
  }

  request(message: DeviceRequest): Promise<unknown> {
    const operation = this.chain.then(() => this.requestExclusive(message));
    this.chain = operation.catch(() => undefined);
    return operation;
  }

  onEvent(listener: (event: AppControlEvent) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async close(): Promise<void> {
    if (this.closed) return;
    this.closed = true;
    this.fail(new Error("USB CDC transport closed"));
    this.child.stdin?.end("__close__\n");
    this.child.kill();
  }

  private requestExclusive(message: DeviceRequest): Promise<unknown> {
    if (this.closed) return Promise.reject(new Error("USB CDC transport is closed"));
    const response = new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = undefined;
        reject(new Error("Device response timed out"));
      }, 5000);
      this.pending = { resolve, reject, timer };
    });
    this.child.stdin!.write(`${JSON.stringify(message)}\n`);
    return response;
  }

  private receive(line: string): void {
    if (line === '{"event":"ready"}') {
      this.readyResolve?.();
      this.readyResolve = undefined;
      this.readyReject = undefined;
      return;
    }
    if (line.length === 0) return;
    let parsed: unknown;
    try { parsed = JSON.parse(line); }
    catch { this.fail(new Error("Malformed JSON from USB CDC")); return; }
    if (
      typeof parsed === "object" && parsed !== null && !Array.isArray(parsed) &&
      (parsed as Record<string, unknown>).event === "input"
    ) {
      try {
        const event = decodeAppControlEvent(parsed);
        for (const listener of this.listeners) listener(event);
      } catch {}
      return;
    }
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = undefined;
    clearTimeout(pending.timer);
    pending.resolve(parsed);
  }

  private fail(error: Error): void {
    this.readyReject?.(error);
    this.readyResolve = undefined;
    this.readyReject = undefined;
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = undefined;
    clearTimeout(pending.timer);
    pending.reject(error);
  }
}
