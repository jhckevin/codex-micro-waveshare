import { PassThrough } from "node:stream";
import { describe, expect, it } from "vitest";

import {
  NativeInputProcessChannel,
  WindowsInput,
  type NativeInputCommand,
  type NativeInputReply,
  type NativeInputChannel,
  type NativeInputSpawn
} from "./windows-input";


class RecordingChannel implements NativeInputChannel {
  readonly commands: NativeInputCommand[] = [];
  reply: NativeInputReply = { ok: true };

  async request(command: NativeInputCommand): Promise<NativeInputReply> {
    this.commands.push(command);
    return this.reply;
  }
}

describe("WindowsInput", () => {
  it("sends separate key down and key up operations", async () => {
    const channel = new RecordingChannel();
    const input = new WindowsInput(channel);

    await input.keyDown("Control");
    await input.keyUp("Control");

    expect(channel.commands).toEqual([
      { op: "key_down", key: "Control" },
      { op: "key_up", key: "Control" }
    ]);
  });

  it("rejects unknown key names before invoking the helper", async () => {
    const channel = new RecordingChannel();
    const input = new WindowsInput(channel);

    await expect(input.keyDown("Control && calc.exe")).rejects.toThrow(
      "Unsupported Windows key"
    );

    expect(channel.commands).toEqual([]);
  });

  it("uses fixed identifiers for built-in application focus", async () => {
    const channel = new RecordingChannel();
    const input = new WindowsInput(channel);

    await input.focusOrLaunch("codex");
    await input.focusOrLaunch("chatgpt");

    expect(channel.commands).toEqual([
      { op: "focus_builtin", app: "codex" },
      { op: "focus_builtin", app: "chatgpt" }
    ]);
  });

  it("accepts an explicit local absolute executable path", async () => {
    const channel = new RecordingChannel();
    const input = new WindowsInput(channel);

    await input.focusOrLaunch("C:\\Program Files\\Example\\Example.exe");

    expect(channel.commands).toEqual([
      {
        op: "focus_or_launch",
        executable: "C:\\Program Files\\Example\\Example.exe"
      }
    ]);
  });

  it.each([
    "Example.exe",
    "..\\Example.exe",
    "\\\\server\\share\\Example.exe",
    "C:\\Program Files\\Example\\Example.cmd",
    "C:\\Program Files\\Example\\Example.exe:stream",
    "C:\\Program Files\\Example\\Example.exe\u0000--flag"
  ])("rejects unsafe executable target %j", async (target) => {
    const channel = new RecordingChannel();
    const input = new WindowsInput(channel);

    await expect(input.focusOrLaunch(target)).rejects.toThrow(
      "Application target must be a local absolute .exe path"
    );

    expect(channel.commands).toEqual([]);
  });

  it("propagates a bounded helper failure", async () => {
    const channel = new RecordingChannel();
    channel.reply = { ok: false, error: "access denied" };
    const input = new WindowsInput(channel);

    await expect(input.keyDown("C")).rejects.toThrow(
      "Windows input helper failed: access denied"
    );
  });

  it("does not expose an unbounded helper error", async () => {
    const channel = new RecordingChannel();
    channel.reply = { ok: false, error: "x".repeat(4000) };
    const input = new WindowsInput(channel);

    await expect(input.keyDown("C")).rejects.toSatisfy((error: unknown) => {
      return error instanceof Error && error.message.length <= 300;
    });
  });
});

describe("NativeInputProcessChannel", () => {
  it("spawns only an explicit absolute helper without a shell", async () => {
    const stdin = new PassThrough();
    const stdout = new PassThrough();
    const stderr = new PassThrough();
    const calls: unknown[][] = [];
    const spawn: NativeInputSpawn = ((...args: unknown[]) => {
      calls.push(args);
      return { stdin, stdout, stderr, kill: () => true };
    }) as NativeInputSpawn;
    const channel = NativeInputProcessChannel.open(
      "C:\\Program Files\\Codex Micro\\codex-input-helper.exe",
      spawn
    );
    stdin.once("data", (data) => {
      const request = JSON.parse(data.toString()) as { id: number };
      stdout.write(`${JSON.stringify({ id: request.id, ok: true })}\n`);
    });

    await expect(
      channel.request({ op: "key_down", key: "C" })
    ).resolves.toEqual({ ok: true });

    expect(calls).toEqual([[
      "C:\\Program Files\\Codex Micro\\codex-input-helper.exe",
      [],
      {
        shell: false,
        windowsHide: true,
        stdio: ["pipe", "pipe", "pipe"]
      }
    ]]);
    channel.close();
  });

  it.each([
    "codex-input-helper.exe",
    "\\\\server\\share\\codex-input-helper.exe",
    "C:\\Temp\\other.exe"
  ])("rejects non-fixed helper path %j", (helperPath) => {
    const spawn = (() => {
      throw new Error("must not spawn");
    }) as NativeInputSpawn;

    expect(() =>
      NativeInputProcessChannel.open(
        helperPath,
        spawn,
        "C:\\Program Files\\Codex Micro"
      )
    ).toThrow("Invalid native helper path");
  });

  it("rejects malformed helper output and closes the process", async () => {
    const stdin = new PassThrough();
    const stdout = new PassThrough();
    const stderr = new PassThrough();
    let killed = false;
    const spawn = (() => ({
      stdin,
      stdout,
      stderr,
      kill: () => { killed = true; return true; }
    })) as NativeInputSpawn;
    const channel = NativeInputProcessChannel.open(
      "C:\\App\\codex-input-helper.exe",
      spawn,
      "C:\\App"
    );
    stdin.once("data", () => stdout.write("{not-json}\n"));

    await expect(
      channel.request({ op: "key_down", key: "C" })
    ).rejects.toThrow("Malformed native helper reply");
    expect(killed).toBe(true);
  });
});
