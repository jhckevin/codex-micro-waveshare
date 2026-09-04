import {
  APP_HEARTBEAT_MS,
  APP_PROTOCOL_VERSION,
  appGoodbyeRequest,
  appHeartbeatRequest,
  appHelloRequest,
  decodeDeviceReply,
  type DeviceReply,
  type DeviceRequest,
  type DeviceTransportKind
} from "../shared/protocol";

export interface DeviceTransport {
  readonly kind: DeviceTransportKind;
  request(message: DeviceRequest): Promise<unknown>;
  close(): void | Promise<void>;
}

export interface IntervalScheduler {
  setInterval(callback: () => void, milliseconds: number): unknown;
  clearInterval(handle: unknown): void;
}

const nativeScheduler: IntervalScheduler = {
  setInterval: (callback, milliseconds) => setInterval(callback, milliseconds),
  clearInterval: (handle) => clearInterval(handle as NodeJS.Timeout)
};

export type DeviceSessionStatus =
  | "disconnected"
  | "connecting"
  | "connected"
  | "disconnecting";

export interface DeviceSessionSnapshot {
  status: DeviceSessionStatus;
  transport: DeviceTransportKind;
  protocol: number | null;
  lastError: string | null;
}

export class DeviceSession {
  private heartbeatHandle: unknown;
  private heartbeatInFlight = false;
  private listeners = new Set<(snapshot: DeviceSessionSnapshot) => void>();
  private current: DeviceSessionSnapshot;

  constructor(
    private readonly transport: DeviceTransport,
    private readonly scheduler: IntervalScheduler = nativeScheduler
  ) {
    this.current = {
      status: "disconnected",
      transport: transport.kind,
      protocol: null,
      lastError: null
    };
  }

  get snapshot(): Readonly<DeviceSessionSnapshot> {
    return this.current;
  }

  subscribe(listener: (snapshot: DeviceSessionSnapshot) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async connect(): Promise<DeviceReply> {
    if (this.current.status !== "disconnected") {
      throw new Error(`Cannot connect while ${this.current.status}`);
    }
    this.update({ status: "connecting", lastError: null });
    try {
      const reply = decodeDeviceReply(await this.transport.request(appHelloRequest()));
      if (!reply.ok || reply.protocol !== APP_PROTOCOL_VERSION) {
        throw new Error(reply.error ?? "Device rejected App session");
      }
      this.update({ status: "connected", protocol: reply.protocol });
      this.heartbeatHandle = this.scheduler.setInterval(
        () => void this.sendHeartbeat(),
        reply.heartbeat_ms === APP_HEARTBEAT_MS
          ? reply.heartbeat_ms
          : APP_HEARTBEAT_MS
      );
      return reply;
    } catch (error) {
      await this.transport.close();
      this.update({
        status: "disconnected",
        protocol: null,
        lastError: error instanceof Error ? error.message : String(error)
      });
      throw error;
    }
  }

  async request(message: DeviceRequest): Promise<DeviceReply> {
    if (this.current.status !== "connected") {
      throw new Error("Device session is not connected");
    }
    return decodeDeviceReply(await this.transport.request(message));
  }

  async disconnect(): Promise<void> {
    if (this.current.status === "disconnected") return;
    this.update({ status: "disconnecting" });
    this.stopHeartbeat();
    try {
      await this.transport.request(appGoodbyeRequest());
    } catch {
      // The independent firmware lease makes an interrupted goodbye fail-safe.
    }
    await this.transport.close();
    this.update({ status: "disconnected", protocol: null });
  }

  private async sendHeartbeat(): Promise<void> {
    if (this.current.status !== "connected" || this.heartbeatInFlight) return;
    this.heartbeatInFlight = true;
    try {
      const reply = decodeDeviceReply(
        await this.transport.request(appHeartbeatRequest())
      );
      if (!reply.ok) throw new Error(reply.error ?? "Heartbeat rejected");
    } catch (error) {
      this.stopHeartbeat();
      await this.transport.close();
      this.update({
        status: "disconnected",
        protocol: null,
        lastError: error instanceof Error ? error.message : String(error)
      });
    } finally {
      this.heartbeatInFlight = false;
    }
  }

  private stopHeartbeat(): void {
    if (this.heartbeatHandle !== undefined) {
      this.scheduler.clearInterval(this.heartbeatHandle);
      this.heartbeatHandle = undefined;
    }
  }

  private update(patch: Partial<DeviceSessionSnapshot>): void {
    this.current = { ...this.current, ...patch };
    for (const listener of this.listeners) listener(this.current);
  }
}
