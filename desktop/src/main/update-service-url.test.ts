import { afterEach, describe, expect, it, vi } from "vitest";
import { canonicalPackageUrl, updateServiceEndpoint } from "./update-service-url";
afterEach(() => vi.unstubAllEnvs());
describe("configurable update service", () => {
  it("does not assume a hosted service", () => {
    vi.stubEnv("CODEX_MICRO_UPDATE_BASE_URL", "");
    expect(() => updateServiceEndpoint("challenges")).toThrow("No update service configured");
  });
  it("supports a self-hosted prefix", () => {
    vi.stubEnv("CODEX_MICRO_UPDATE_BASE_URL", "https://example.org/custom/");
    expect(updateServiceEndpoint("challenges").href).toBe("https://example.org/custom/v1/challenges");
    expect(canonicalPackageUrl("https://example.org/custom/v1/packages/latest?class=compatibility").origin).toBe("https://example.org");
    expect(() => canonicalPackageUrl("https://evil.example/custom/v1/packages/latest")).toThrow();
    expect(() => canonicalPackageUrl("https://example.org/v1/packages/latest")).toThrow();
  });
  it("rejects insecure services", () => {
    vi.stubEnv("CODEX_MICRO_UPDATE_BASE_URL", "http://example.org/");
    expect(() => updateServiceEndpoint("attest")).toThrow();
  });
});
