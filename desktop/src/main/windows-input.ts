import { spawn as nodeSpawn } from "node:child_process";
import path from "node:path";
import { createInterface } from "node:readline";
import type { Readable, Writable } from "node:stream";

import type { WindowsInputAdapter } from "./action-engine";


export type NativeInputCommand =
  | { op: "key_down"; key: string }
  | { op: "key_up"; key: string }
  | { op: "focus_builtin"; app: "codex" | "chatgpt" }
  | { op: "focus_or_launch"; executable: string };

export type NativeInputReply =
  | { ok: true }
  | { ok: false; error?: string };

export interface NativeInputChannel {
  request(command: NativeInputCommand): Promise<NativeInputReply>;
}

export interface NativeInputProcess {
  stdin: Writable;
  stdout: Readable;
  stderr: Readable;
  kill(): boolean;
  once?(event: "exit" | "error", listener: (...args: unknown[]) => void): unknown;
}

export type NativeInputSpawn = (
  executable: string,
  args: readonly string[],
  options: {
    shell: false;
    windowsHide: true;
    stdio: ["pipe", "pipe", "pipe"];
  }
) => NativeInputProcess;

interface PendingReply {
  resolve(reply: NativeInputReply): void;
  reject(error: Error): void;
}

const SUPPORTED_KEYS = new Set([
  "Alt",
  "ArrowDown",
  "ArrowLeft",
  "ArrowRight",
  "ArrowUp",
  "Backspace",
  "Control",
  "Delete",
  "End",
  "Enter",
  "Escape",
  "Home",
  "Insert",
  "PageDown",
  "PageUp",
  "RightAlt",
  "Shift",
  "Space",
  "Tab",
  "Windows",
  ...Array.from({ length: 26 }, (_, index) =>
    String.fromCharCode("A".charCodeAt(0) + index)
  ),
  ...Array.from({ length: 10 }, (_, index) => String(index)),
  ...Array.from({ length: 24 }, (_, index) => `F${index + 1}`)
]);

const BUILTIN_APPS = new Set(["codex", "chatgpt"]);
const MAX_HELPER_ERROR = 240;

function validateKey(key: string): void {
  if (!SUPPORTED_KEYS.has(key)) {
    throw new TypeError(`Unsupported Windows key: ${key.slice(0, 64)}`);
  }
}

function executableTarget(target: string): string | undefined {
  if (
    typeof target !== "string" ||
    target.length < 7 ||
    target.length > 1024 ||
    !/^[A-Za-z]:\\/.test(target) ||
    !/\.exe$/i.test(target) ||
    /[\u0000-\u001f]/.test(target) ||
    target.includes(":\\", 2) ||
    target.includes("/../") ||
    target.includes("\\..\\")
  ) {
    return undefined;
  }
  return target;
}

function helperError(reply: NativeInputReply): Error | undefined {
  if (reply.ok) return undefined;
  const detail =
    typeof reply.error === "string" && reply.error.length > 0
      ? reply.error.slice(0, MAX_HELPER_ERROR)
      : "unknown error";
  return new Error(`Windows input helper failed: ${detail}`);
}

export class WindowsInput implements WindowsInputAdapter {
  constructor(private readonly channel: NativeInputChannel) {}

  async keyDown(key: string): Promise<void> {
    validateKey(key);
    await this.execute({ op: "key_down", key });
  }

  async keyUp(key: string): Promise<void> {
    validateKey(key);
    await this.execute({ op: "key_up", key });
  }

  async focusOrLaunch(target: string): Promise<void> {
    if (BUILTIN_APPS.has(target)) {
      await this.execute({
        op: "focus_builtin",
        app: target as "codex" | "chatgpt"
      });
      return;
    }
    const executable = executableTarget(target);
    if (executable === undefined) {
      throw new TypeError(
        "Application target must be a local absolute .exe path"
      );
    }
    await this.execute({ op: "focus_or_launch", executable });
  }

  private async execute(command: NativeInputCommand): Promise<void> {
    const reply = await this.channel.request(command);
    const error = helperError(reply);
    if (error !== undefined) throw error;
  }
}

function isLocalAbsoluteHelper(
  helperPath: string,
  expectedDirectory: string
): boolean {
  if (
    !/^[A-Za-z]:\\/.test(helperPath) ||
    helperPath.startsWith("\\\\") ||
    /[\u0000-\u001f]/.test(helperPath) ||
    path.win32.basename(helperPath).toLowerCase() !==
      "codex-input-helper.exe"
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
): NativeInputProcess {
  return nodeSpawn(executable, [...args], options);
}

export class NativeInputProcessChannel implements NativeInputChannel {
  private readonly pending = new Map<number, PendingReply>();
  private nextId = 1;
  private closed = false;

  private constructor(private readonly process: NativeInputProcess) {
    const lines = createInterface({ input: process.stdout });
    lines.on("line", (line) => this.receive(line));
    process.once?.("exit", () =>
      this.fail(new Error("Windows input helper exited"))
    );
    process.once?.("error", () =>
      this.fail(new Error("Windows input helper failed to start"))
    );
  }

  static open(
    helperPath: string,
    spawn: NativeInputSpawn = defaultSpawn,
    expectedDirectory = path.win32.dirname(helperPath)
  ): NativeInputProcessChannel {
    if (!isLocalAbsoluteHelper(helperPath, expectedDirectory)) {
      throw new TypeError("Invalid native helper path");
    }
    const child = spawn(helperPath, [], {
      shell: false,
      windowsHide: true,
      stdio: ["pipe", "pipe", "pipe"]
    });
    return new NativeInputProcessChannel(child);
  }

  request(command: NativeInputCommand): Promise<NativeInputReply> {
    if (this.closed) {
      return Promise.reject(new Error("Windows input helper is closed"));
    }
    const id = this.nextId;
    this.nextId = this.nextId === 0x7fffffff ? 1 : this.nextId + 1;
    return new Promise<NativeInputReply>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      const line = `${JSON.stringify({ id, ...command })}\n`;
      this.process.stdin.write(line, (error) => {
        if (error === null || error === undefined) return;
        this.pending.delete(id);
        reject(new Error("Windows input helper write failed"));
      });
    });
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.process.kill();
    const error = new Error("Windows input helper is closed");
    for (const pending of this.pending.values()) pending.reject(error);
    this.pending.clear();
  }

  private receive(line: string): void {
    let value: unknown;
    try {
      value = JSON.parse(line);
    } catch {
      this.fail(new Error("Malformed native helper reply"));
      return;
    }
    if (
      typeof value !== "object" ||
      value === null ||
      !Number.isSafeInteger((value as { id?: unknown }).id) ||
      typeof (value as { ok?: unknown }).ok !== "boolean"
    ) {
      this.fail(new Error("Malformed native helper reply"));
      return;
    }
    const reply = value as { id: number; ok: boolean; error?: unknown };
    const pending = this.pending.get(reply.id);
    if (pending === undefined) {
      this.fail(new Error("Unexpected native helper reply"));
      return;
    }
    if (
      reply.error !== undefined &&
      typeof reply.error !== "string"
    ) {
      this.fail(new Error("Malformed native helper reply"));
      return;
    }
    this.pending.delete(reply.id);
    pending.resolve(
      reply.ok
        ? { ok: true }
        : { ok: false, error: reply.error?.slice(0, MAX_HELPER_ERROR) }
    );
  }

  private fail(error: Error): void {
    if (this.closed) return;
    this.closed = true;
    this.process.kill();
    for (const pending of this.pending.values()) pending.reject(error);
    this.pending.clear();
  }
}
