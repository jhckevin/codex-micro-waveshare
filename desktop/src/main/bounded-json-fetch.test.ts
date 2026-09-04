import { describe, expect, it, vi } from "vitest";

import { fetchBoundedJson } from "./bounded-json-fetch";

describe("fetchBoundedJson", () => {
  it("returns a parsed response within the declared and streamed limits", async () => {
    const fetcher = vi.fn(async () => new Response(
      JSON.stringify({ ok: true }),
      { status: 200, headers: { "content-length": "11" } }
    ));

    const result = await fetchBoundedJson(
      new URL("https://example.org/codex-micro/v1/challenges"),
      { method: "POST" },
      { maximumBytes: 64, timeoutMs: 1000 },
      fetcher
    );

    expect(result).toEqual({ ok: true, status: 200, value: { ok: true } });
    expect(fetcher).toHaveBeenCalledOnce();
    expect(fetcher.mock.calls[0][1]).toMatchObject({ cache: "no-store" });
  });

  it("rejects an oversized declared response before reading it", async () => {
    const fetcher = vi.fn(async () => new Response("{}", {
      status: 200,
      headers: { "content-length": "65" }
    }));

    await expect(fetchBoundedJson(
      "https://example.org/codex-micro/v1/packages/latest",
      {},
      { maximumBytes: 64, timeoutMs: 1000 },
      fetcher
    )).rejects.toThrow("declared size exceeds 64 bytes");
  });

  it("rejects an oversized decoded stream even without content-length", async () => {
    const fetcher = vi.fn(async () => new Response("x".repeat(65)));

    await expect(fetchBoundedJson(
      "https://example.org/codex-micro/v1/packages/latest",
      {},
      { maximumBytes: 64, timeoutMs: 1000 },
      fetcher
    )).rejects.toThrow("body exceeds 64 bytes");
  });

  it("aborts a stalled request at the configured deadline", async () => {
    const fetcher: typeof fetch = vi.fn((_input, init) =>
      new Promise((_resolve, reject) => {
        init?.signal?.addEventListener("abort", () =>
          reject(init.signal?.reason), { once: true });
      })
    );

    await expect(fetchBoundedJson(
      "https://example.org/codex-micro/v1/challenges",
      {},
      { maximumBytes: 64, timeoutMs: 10 },
      fetcher
    )).rejects.toThrow("timed out after 10 ms");
  });
});
