export interface BoundedFetchLimits {
  maximumBytes: number;
  timeoutMs: number;
}

export interface BoundedJsonResponse {
  ok: boolean;
  status: number;
  value: unknown;
}

export async function fetchBoundedJson(
  input: URL | string,
  init: RequestInit,
  limits: BoundedFetchLimits,
  fetcher: typeof fetch = fetch
): Promise<BoundedJsonResponse> {
  if (!Number.isSafeInteger(limits.maximumBytes) ||
      limits.maximumBytes < 2 ||
      !Number.isSafeInteger(limits.timeoutMs) ||
      limits.timeoutMs < 1) {
    throw new RangeError("Invalid bounded fetch limits");
  }
  const controller = new AbortController();
  const timer = setTimeout(() => {
    controller.abort(new Error(
      `Update service request timed out after ${limits.timeoutMs} ms`));
  }, limits.timeoutMs);
  try {
    const response = await fetcher(input, {
      ...init,
      cache: "no-store",
      redirect: "error",
      signal: controller.signal
    });
    const declared = response.headers.get("content-length");
    if (declared !== null) {
      const declaredBytes = Number(declared);
      if (!Number.isSafeInteger(declaredBytes) || declaredBytes < 0) {
        throw new Error("Update service returned an invalid content length");
      }
      if (declaredBytes > limits.maximumBytes) {
        throw new Error(
          `Update service declared size exceeds ${limits.maximumBytes} bytes`);
      }
    }
    if (response.body === null) {
      throw new Error("Update service returned an empty response");
    }
    const reader = response.body.getReader();
    const chunks: Uint8Array[] = [];
    let used = 0;
    while (true) {
      const next = await reader.read();
      if (next.done) break;
      used += next.value.byteLength;
      if (used > limits.maximumBytes) {
        await reader.cancel();
        throw new Error(
          `Update service body exceeds ${limits.maximumBytes} bytes`);
      }
      chunks.push(next.value);
    }
    const bytes = new Uint8Array(used);
    let offset = 0;
    for (const chunk of chunks) {
      bytes.set(chunk, offset);
      offset += chunk.byteLength;
    }
    const text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    return {
      ok: response.ok,
      status: response.status,
      value: JSON.parse(text) as unknown
    };
  } catch (error) {
    if (controller.signal.aborted) {
      throw controller.signal.reason;
    }
    throw error;
  } finally {
    clearTimeout(timer);
  }
}
