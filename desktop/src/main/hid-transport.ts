import type { DeviceTransport } from "./device-session";
import {
  decodeAppControlEvent,
  type AppControlEvent,
  type DeviceRequest
} from "../shared/protocol";

const VENDOR_ID = 0x303a;
const PRODUCT_ID = 0x8360;
const CONFIG_REPORT_ID = 7;
const REPORT_BODY_SIZE = 63;
const PAYLOAD_SIZE = 61;
const MAX_RECEIVE_TEXT = 8192;

export interface AsyncHidDevice {
  write(data: number[]): Promise<number>;
  close(): Promise<void>;
  on(event: "data", listener: (data: Buffer) => void): void;
  on(event: "error", listener: (error: Error) => void): void;
}

export interface HidProvider {
  devicesAsync(): Promise<Array<{
    vendorId: number;
    productId: number;
    path?: string;
    serialNumber?: string;
  }>>;
  openPath(path: string): Promise<AsyncHidDevice>;
}

export class CodexHidTransport implements DeviceTransport {
  readonly kind = "usb" as const;
  private receiveText = "";
  private pending?: {
    resolve(value: unknown): void;
    reject(error: Error): void;
    timer: NodeJS.Timeout;
  };
  private chain: Promise<unknown> = Promise.resolve();
  private readonly eventListeners = new Set<(event: AppControlEvent) => void>();

  constructor(private readonly device: AsyncHidDevice) {
    device.on("data", (data) => this.onData(data));
    device.on("error", (error) => this.failPending(error));
  }

  static async open(provider?: HidProvider): Promise<CodexHidTransport> {
    const hid = provider ?? await loadNodeHid();
    const devices = await hid.devicesAsync();
    const match = devices.find(
      (device) =>
        device.vendorId === VENDOR_ID &&
        device.productId === PRODUCT_ID &&
        typeof device.path === "string"
    );
    if (!match?.path) throw new Error("Codex Micro USB HID is not connected");
    return new CodexHidTransport(await hid.openPath(match.path));
  }

  request(message: DeviceRequest): Promise<unknown> {
    const operation = this.chain.then(() => this.requestExclusive(message));
    this.chain = operation.catch(() => undefined);
    return operation;
  }

  onEvent(listener: (event: AppControlEvent) => void): () => void {
    this.eventListeners.add(listener);
    return () => this.eventListeners.delete(listener);
  }

  async close(): Promise<void> {
    this.failPending(new Error("USB transport closed"));
    this.eventListeners.clear();
    await this.device.close();
  }

  private async requestExclusive(message: DeviceRequest): Promise<unknown> {
    if (this.pending) throw new Error("Concurrent HID request");
    const encoded = Buffer.from(`${JSON.stringify(message)}\n`, "utf8");
    const response = new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = undefined;
        reject(new Error("Device response timed out"));
      }, 5000);
      this.pending = { resolve, reject, timer };
    });
    for (let offset = 0; offset < encoded.length; offset += PAYLOAD_SIZE) {
      const chunk = encoded.subarray(offset, offset + PAYLOAD_SIZE);
      const report = new Array<number>(REPORT_BODY_SIZE + 1).fill(0);
      report[0] = CONFIG_REPORT_ID;
      report[1] = 2;
      report[2] = chunk.length;
      for (let index = 0; index < chunk.length; index += 1) {
        report[index + 3] = chunk[index];
      }
      await this.device.write(report);
    }
    return response;
  }

  private onData(raw: Buffer): void {
    let report = raw;
    if (report[0] === CONFIG_REPORT_ID) report = report.subarray(1);
    if (report.length < 2 || report[0] !== 2 || report[1] > PAYLOAD_SIZE) return;
    this.receiveText += report.subarray(2, 2 + report[1]).toString("utf8");
    if (this.receiveText.length > MAX_RECEIVE_TEXT) {
      this.receiveText = "";
      this.failPending(new Error("Device response exceeded receive limit"));
      return;
    }
    let newline = this.receiveText.indexOf("\n");
    while (newline >= 0) {
      const line = this.receiveText.slice(0, newline);
      this.receiveText = this.receiveText.slice(newline + 1);
      if (line.length > 0) {
        let parsed: unknown;
        try {
          parsed = JSON.parse(line);
        } catch {
          this.failPending(new Error("Malformed JSON from device"));
          newline = this.receiveText.indexOf("\n");
          continue;
        }
        if (
          typeof parsed === "object" &&
          parsed !== null &&
          !Array.isArray(parsed) &&
          (parsed as Record<string, unknown>).event === "input"
        ) {
          try {
            const event = decodeAppControlEvent(parsed);
            for (const listener of this.eventListeners) listener(event);
          } catch {
            // Invalid private input is isolated from the RPC response stream.
          }
          newline = this.receiveText.indexOf("\n");
          continue;
        }
        if (!this.pending) {
          newline = this.receiveText.indexOf("\n");
          continue;
        }
        const pending = this.pending;
        this.pending = undefined;
        clearTimeout(pending.timer);
        pending.resolve(parsed);
      }
      newline = this.receiveText.indexOf("\n");
    }
  }

  private failPending(error: Error): void {
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = undefined;
    clearTimeout(pending.timer);
    pending.reject(error);
  }
}

async function loadNodeHid(): Promise<HidProvider> {
  const module = await import("node-hid");
  return {
    devicesAsync: () => module.devicesAsync(),
    openPath: (path) => module.HIDAsync.open(path)
  };
}
