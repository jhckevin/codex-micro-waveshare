const {spawnSync} = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

async function json(url, options = {}) {
  const response = await fetch(url, options);
  if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
  return response.json();
}

function releaseSequence(updatePackage) {
  const manifest = Buffer.from(updatePackage.manifest_hex, "hex");
  if (manifest.length !== 248 || manifest.subarray(0, 4).toString("ascii") !== "CMU1") {
    throw new Error("invalid update manifest in public response");
  }
  return manifest.readBigUInt64LE(24).toString();
}

async function main() {
  const [base, helper, outputDir] = process.argv.slice(2);
  const deviceId = "637409e5a60ac8c1232798ce7c9d413a";
  const challenge = await json(`${base}/v1/challenges`, {
    method: "POST", headers: {"content-type": "application/json"},
    body: JSON.stringify({device_id: deviceId})
  });
  const probe = spawnSync(process.execPath, [path.join(__dirname, "cdc_update_probe.cjs"),
    helper, "attest", challenge.challenge], {encoding: "utf8"});
  if (probe.status !== 0) throw new Error(probe.stderr || "device attest failed");
  const proof = JSON.parse(probe.stdout).attest;
  const session = await json(`${base}/v1/attest`, {
    method: "POST", headers: {"content-type": "application/json"},
    body: JSON.stringify({challenge_id: challenge.challenge_id,
      device_id: proof.device_id, tag: proof.tag})
  });
  const summary = {device_attested: proof.device_id === deviceId, packages: {}};
  for (const packageClass of ["user_content", "complete_firmware"]) {
    const updatePackage = await json(`${base}/v1/packages/latest?class=${packageClass}`,
      {headers: {authorization: `Bearer ${session.token}`}});
    const sequence = releaseSequence(updatePackage);
    const output = path.join(outputDir, `public-${packageClass}-seq${sequence}.json`);
    fs.writeFileSync(output, JSON.stringify(updatePackage));
    summary.packages[packageClass] = {target_version: updatePackage.target_version,
      release_sequence: sequence, chunks: updatePackage.chunks.length,
      download_bytes: fs.statSync(output).size, output};
  }
  process.stdout.write(`${JSON.stringify(summary)}\n`);
}

main().catch((error) => { console.error(error.stack || error); process.exitCode = 1; });
