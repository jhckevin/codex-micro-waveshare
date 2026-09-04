#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const hid = require("node-hid");

const VID = 0x303a;
const PID = 0x8360;
const REPORT_ID = 7;
const PAYLOAD_SIZE = 61;

class Link {
  constructor(device) {
    this.device = device;
    this.text = "";
    this.pending = null;
    device.on("data", (data) => this.onData(data));
    device.on("error", (error) => this.reject(error));
  }

  onData(raw) {
    if (process.env.CODEX_HID_DEBUG === "1") console.error("RX", raw.toString("hex"));
    let report = raw;
    if (report[0] === REPORT_ID) report = report.subarray(1);
    if (report.length < 2 || report[0] !== 2 || report[1] > PAYLOAD_SIZE) return;
    this.text += report.subarray(2, 2 + report[1]).toString("utf8");
    let newline;
    while ((newline = this.text.indexOf("\n")) >= 0) {
      const line = this.text.slice(0, newline);
      this.text = this.text.slice(newline + 1);
      if (!line || !this.pending) continue;
      let value;
      try { value = JSON.parse(line); } catch { continue; }
      if (value && value.event === "input") continue;
      const pending = this.pending;
      this.pending = null;
      clearTimeout(pending.timer);
      pending.resolve(value);
    }
  }

  reject(error) {
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = null;
    clearTimeout(pending.timer);
    pending.reject(error);
  }

  async request(message) {
    if (this.pending) throw new Error("concurrent request");
    const encoded = Buffer.from(`${JSON.stringify(message)}\n`, "utf8");
    const response = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = null;
        reject(new Error(`timeout: ${message.method}`));
      }, 7000);
      this.pending = {resolve, reject, timer};
    });
    for (let offset = 0; offset < encoded.length; offset += PAYLOAD_SIZE) {
      const chunk = encoded.subarray(offset, offset + PAYLOAD_SIZE);
      const report = new Array(64).fill(0);
      report[0] = REPORT_ID;
      report[1] = 2;
      report[2] = chunk.length;
      for (let index = 0; index < chunk.length; ++index) report[index + 3] = chunk[index];
      await this.device.write(report);
      if (process.env.CODEX_HID_DEBUG === "1") console.error("TX", Buffer.from(report).toString("hex"));
    }
    return response;
  }
}

async function checked(link, message) {
  const reply = await link.request(message);
  if (!reply || reply.ok !== true) throw new Error(`${message.method}: ${JSON.stringify(reply)}`);
  return reply;
}

async function install(link, updatePackage) {
  const timeline = [];
  let reply = await checked(link, {
    method: "update.begin",
    params: {manifest: updatePackage.manifest_hex}
  });
  timeline.push(reply);
  for (const chunk of updatePackage.chunks) {
    reply = await checked(link, {
      method: "update.chunk",
      params: {
        offset: chunk.offset,
        sequence: chunk.sequence,
        data: chunk.ciphertext_hex,
        tag: chunk.tag_hex,
        sha256: chunk.plaintext_sha256_hex
      }
    });
    timeline.push(reply);
  }
  reply = await checked(link, {method: "update.commit"});
  timeline.push(reply);
  return timeline;
}

async function main() {
  const packagePath = process.argv[2];
  const packageClass = process.argv[3];
  const devices = await hid.devicesAsync();
  const candidates = devices.filter((item) => item.vendorId === VID && item.productId === PID && item.path);
  if (process.env.CODEX_HID_LIST === "1") {
    process.stdout.write(`${JSON.stringify(candidates, null, 2)}\n`);
    return;
  }
  const requestedIndex = Number(process.env.CODEX_HID_INDEX || 0);
  const match = candidates[requestedIndex];
  if (!match) throw new Error("Codex Micro HID not found");
  const device = await hid.HIDAsync.open(match.path);
  const link = new Link(device);
  try {
    const hello = await checked(link, {method: "app.hello", params: {protocol: 1}});
    const heartbeat = await checked(link, {method: "app.heartbeat", params: {protocol: 1}});
    const evidence = {
      time: new Date().toISOString(),
      candidates: candidates.map(({path, interface: interfaceNumber, usage, usagePage}) =>
        ({path, interface: interfaceNumber, usage, usagePage})),
      path: match.path,
      hello,
      heartbeat,
      update: await checked(link, {method: "update.hello"})
    };
    evidence.status = await checked(link, {method: "update.status"});
    if (process.env.CODEX_HID_PROFILE === "1") {
      evidence.profile = [];
      evidence.profile.push(await checked(link, {
        method: "routing.set",
        params: {
          enabled: true,
          layer_count: 6,
          layer1_command_targets: [0, 0, 0, 0, 0, 0],
          higher_codex_masks: Array.from({length: 5}, () => [63, 63, 63, 63])
        }
      }));
      const commandIds = ["FAST", "APPR", "REJ", "COMPUTER", "MIC", "OAI"];
      for (let layer = 1; layer <= 6; ++layer) {
        evidence.profile.push(await checked(link, {
          method: "icons.set",
          params: {layer, group: "agent", ids: Array.from({length: 6}, (_, index) => `AGENT-${index + 1}`)}
        }));
        evidence.profile.push(await checked(link, {
          method: "icons.set",
          params: {layer, group: "command", ids: commandIds}
        }));
      }
    }
    if (process.env.CODEX_HID_STRESS === "1") {
      evidence.stress = [];
      for (let index = 0; index < 30; ++index) {
        evidence.stress.push(await checked(link, index % 3 === 0
          ? {method: "app.heartbeat", params: {protocol: 1}}
          : index % 3 === 1
            ? {method: "cfg.get_state"}
            : {method: "update.status"}));
        await new Promise((resolve) => setTimeout(resolve, 150));
      }
    }
    if (packageClass === "confirm") {
      evidence.confirm = await checked(link, {method: "update.confirm"});
      evidence.after = await checked(link, {method: "update.status"});
    } else if (packagePath && packageClass) {
      const catalog = JSON.parse(fs.readFileSync(packagePath, "utf8"));
      const packages = catalog.devices ? catalog.devices[Object.keys(catalog.devices)[0]] : catalog;
      if (!packages[packageClass]) throw new Error(`missing package class ${packageClass}`);
      evidence.package_class = packageClass;
      evidence.timeline = await install(link, packages[packageClass]);
      evidence.after = await checked(link, {method: "update.status"});
    }
    process.stdout.write(`${JSON.stringify(evidence, null, 2)}\n`);
  } finally {
    await device.close();
  }
}

main().catch((error) => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
