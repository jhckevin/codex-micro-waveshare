import { randomUUID } from "node:crypto";
import { link, unlink, writeFile } from "node:fs/promises";
import path from "node:path";


const ARGUMENT_PREFIX = "--release-smoke-output=";
const MARKER_NAME = /^codex-micro-smoke-[0-9a-f]{32}\.json$/;

export interface ReleaseSmokeMarker {
  schema: 1;
  packaged: boolean;
  platform: string;
  arch: string;
  package_version: string;
  renderer_loaded: true;
}

export function releaseSmokeOutput(
  argv: readonly string[]
): string | undefined {
  const values = argv
    .filter((argument) => argument.startsWith(ARGUMENT_PREFIX))
    .map((argument) => argument.slice(ARGUMENT_PREFIX.length));
  if (values.length === 0) return undefined;
  if (values.length !== 1 || values[0].length > 1024) {
    throw new Error("Malformed release smoke output argument");
  }
  const output = path.resolve(values[0]);
  if (!path.isAbsolute(values[0]) ||
      !MARKER_NAME.test(path.basename(output))) {
    throw new Error("Release smoke output must be an absolute nonce marker");
  }
  return output;
}

export async function writeReleaseSmokeMarker(
  output: string,
  marker: ReleaseSmokeMarker
): Promise<void> {
  if (!path.isAbsolute(output) ||
      !MARKER_NAME.test(path.basename(output))) {
    throw new Error("Invalid release smoke marker path");
  }
  if (
    marker.schema !== 1 ||
    typeof marker.packaged !== "boolean" ||
    marker.platform.length === 0 ||
    marker.platform.length > 32 ||
    marker.arch.length === 0 ||
    marker.arch.length > 32 ||
    !/^\d+\.\d+\.\d+$/.test(marker.package_version) ||
    marker.renderer_loaded !== true
  ) {
    throw new Error("Invalid release smoke marker");
  }
  const serialized = JSON.stringify(marker) + "\n";
  const temporary = path.join(
    path.dirname(output),
    `.${path.basename(output)}.${randomUUID()}.tmp`
  );
  await writeFile(temporary, serialized, {
    encoding: "utf8",
    flag: "wx",
    mode: 0o600
  });
  try {
    // Publish only complete evidence, without ever replacing an existing marker.
    await link(temporary, output);
  } finally {
    await unlink(temporary);
  }
}
