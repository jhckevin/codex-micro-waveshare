#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const {spawn} = require("node:child_process");
const path = require("node:path");

class CdcLink {
  constructor(helperPath) {
    this.child = spawn("powershell.exe", [
      "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
      "-File", path.resolve(helperPath)
    ], {stdio: ["pipe", "pipe", "pipe"]});
    this.buffer = "";
    this.pending = null;
    this.ready = new Promise((resolve, reject) => {
      this.readyResolve = resolve;
      this.readyReject = reject;
    });
    this.child.stdout.on("data", (data) => this.onData(data));
    this.child.stderr.on("data", (data) => process.stderr.write(data));
    this.child.on("error", (error) => this.fail(error));
    this.child.on("exit", (code) => this.fail(new Error(`CDC helper exited: ${code}`)));
  }

  onData(data) {
    this.buffer += data.toString("utf8");
    let newline;
    while ((newline = this.buffer.indexOf("\n")) >= 0) {
      const line = this.buffer.slice(0, newline).trimEnd();
      this.buffer = this.buffer.slice(newline + 1);
      if (!line) continue;
      let value;
      try { value = JSON.parse(line); } catch { continue; }
      if (value.event === "ready") {
        this.readyResolve();
        continue;
      }
      if (value.event === "input") continue;
      if (!this.pending) continue;
      const pending = this.pending;
      this.pending = null;
      clearTimeout(pending.timer);
      pending.resolve(value);
    }
  }

  fail(error) {
    this.readyReject?.(error);
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = null;
    clearTimeout(pending.timer);
    pending.reject(error);
  }

  async request(message, timeoutMs = 15000) {
    await this.ready;
    if (this.pending) throw new Error("concurrent CDC request");
    const response = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = null;
        reject(new Error(`timeout: ${message.method}`));
      }, timeoutMs);
      this.pending = {resolve, reject, timer};
    });
    this.child.stdin.write(`${JSON.stringify(message)}\n`);
    const reply = await response;
    if (!reply || reply.ok !== true) {
      throw new Error(`${message.method}: ${JSON.stringify(reply)}`);
    }
    return reply;
  }

  async close() {
    if (!this.child.killed) {
      this.child.stdin.end("__close__\n");
      await new Promise((resolve) => {
        const timer = setTimeout(() => { this.child.kill(); resolve(); }, 2000);
        this.child.once("exit", () => { clearTimeout(timer); resolve(); });
      });
    }
  }
}

function packageFromCatalog(catalogPath, packageClass) {
  const catalog = JSON.parse(fs.readFileSync(catalogPath, "utf8"));
  if (catalog?.package_class === packageClass) return catalog;
  const packages = catalog.devices
    ? catalog.devices[Object.keys(catalog.devices)[0]]
    : catalog;
  if (!packages?.[packageClass]) throw new Error(`missing package class: ${packageClass}`);
  return packages[packageClass];
}

async function main() {
  const helperPath = process.argv[2];
  const action = process.argv[3] || "status";
  const catalogPath = process.argv[4];
  const packageClass = process.argv[5];
  const stopAfterChunks = Number(process.env.CODEX_STOP_AFTER_CHUNKS || 0);
  const heartbeatEvery = Number(process.env.CODEX_HEARTBEAT_EVERY || 256);
  const evidence = {started_at: new Date().toISOString(), action, milestones: []};
  const link = new CdcLink(helperPath);
  try {
    evidence.hello = await link.request({method: "app.hello", params: {protocol: 1}});
    evidence.update_hello = await link.request({method: "update.hello"});
    evidence.before = await link.request({method: "update.status"});
    if (action === "attest") {
      const challenge = process.argv[4];
      if (!/^[0-9a-f]{64}$/i.test(challenge || "")) {
        throw new Error("attest requires a 32-byte hex challenge");
      }
      evidence.attest = await link.request({
        method: "update.attest", params: {challenge}
      });
      return;
    }
    if (action === "status") return;
    if (action === "confirm") {
      evidence.confirm = await link.request({method: "update.confirm"});
      evidence.after = await link.request({method: "update.status"});
      return;
    }
    if (action === "cancel") {
      evidence.cancel = await link.request({method: "update.cancel"});
      evidence.after = await link.request({method: "update.status"});
      return;
    }
    if (action === "reboot") {
      evidence.reboot = await link.request({method: "cfg.reboot"});
      return;
    }
    if (action !== "install") throw new Error(`unknown action: ${action}`);
    const updatePackage = packageFromCatalog(catalogPath, packageClass);
    evidence.package = {
      package_class: updatePackage.package_class,
      target_version: updatePackage.target_version,
      chunks: updatePackage.chunks.length,
      ciphertext_bytes: updatePackage.chunks.reduce(
        (sum, chunk) => sum + chunk.ciphertext_hex.length / 2, 0)
    };
    let progress = evidence.before;
    const resumable = process.env.CODEX_FORCE_BEGIN !== "1" &&
      progress.state === "receiving" &&
      progress.total_size === evidence.package.ciphertext_bytes &&
      progress.target_version === updatePackage.target_version;
    if (!resumable) {
      progress = await link.request({
        method: "update.begin", params: {manifest: updatePackage.manifest_hex}
      }, 30000);
    }
    evidence.resumed_existing_session = resumable;
    evidence.begin = progress;
    const started = Date.now();
    let sent = 0;
    for (const chunk of updatePackage.chunks) {
      if (chunk.offset < progress.expected_offset) continue;
      if (chunk.offset !== progress.expected_offset ||
          chunk.sequence !== progress.next_sequence) {
        throw new Error(`resume mismatch at offset ${progress.expected_offset}`);
      }
      progress = await link.request({method: "update.chunk", params: {
        offset: chunk.offset,
        sequence: chunk.sequence,
        data: chunk.ciphertext_hex,
        tag: chunk.tag_hex,
        sha256: chunk.plaintext_sha256_hex
      }});
      sent += 1;
      if (sent === 1 || sent % 512 === 0 || sent === updatePackage.chunks.length) {
        evidence.milestones.push({sent, elapsed_ms: Date.now() - started,
          expected_offset: progress.expected_offset, next_sequence: progress.next_sequence});
      }
      if (heartbeatEvery > 0 && sent % heartbeatEvery === 0) {
        evidence.last_heartbeat = await link.request({
          method: "app.heartbeat", params: {protocol: 1}
        });
      }
      if (stopAfterChunks > 0 && sent >= stopAfterChunks) {
        evidence.interrupted = true;
        evidence.interrupted_status = await link.request({method: "update.status"});
        return;
      }
    }
    evidence.commit = await link.request({method: "update.commit"}, 45000);
    evidence.after = await link.request({method: "update.status"});
  } finally {
    evidence.finished_at = new Date().toISOString();
    process.stdout.write(`${JSON.stringify(evidence, null, 2)}\n`);
    await link.close();
  }
}

main().catch((error) => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
