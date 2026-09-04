// No hosted service is assumed by the source-only distribution.
export type UpdateServiceEndpoint = "challenges" | "attest" | "packages/latest";
function serviceBase(): URL {
  const value = process.env.CODEX_MICRO_UPDATE_BASE_URL;
  if (!value) throw new Error("No update service configured; build and flash firmware locally.");
  const base = new URL(value);
  if (base.protocol !== "https:" || base.username || base.password || base.search || base.hash) {
    throw new Error("Update service must be an HTTPS URL without credentials, query or fragment");
  }
  if (!base.pathname.endsWith("/")) base.pathname += "/";
  return base;
}
export function updateServiceEndpoint(endpoint: UpdateServiceEndpoint): URL {
  return new URL(`v1/${endpoint}`, serviceBase());
}
export function canonicalPackageUrl(value: string): URL {
  const expected = updateServiceEndpoint("packages/latest");
  const candidate = new URL(value);
  if (candidate.protocol !== "https:" || candidate.origin !== expected.origin ||
      candidate.pathname !== expected.pathname || candidate.username || candidate.password || candidate.hash) {
    throw new Error("Update URL is outside the canonical HTTPS service");
  }
  return candidate;
}
