import {
  createContext,
  useContext,
  useEffect,
  useMemo,
  useSyncExternalStore,
  type ReactNode
} from "react";

import {
  DeviceStore,
  type DeviceBridge,
  type DeviceStoreSnapshot
} from "./device-store";


const unavailable: DeviceBridge = {
  getStatus: async () => ({
    status: "disconnected",
    transport: "usb",
    protocol: null,
    lastError: "Desktop bridge unavailable"
  }),
  connect: async () => { throw new Error("Desktop bridge unavailable"); },
  disconnect: async () => undefined,
  request: async () => { throw new Error("Desktop bridge unavailable"); }
};

const DeviceContext = createContext<DeviceStore | null>(null);

export function DeviceProvider({ children }: { children: ReactNode }) {
  const store = useMemo(
    () => new DeviceStore(window.codexMicro ?? unavailable),
    []
  );
  useEffect(() => {
    void store.refresh().catch(() => undefined);
    const refreshHandle = window.setInterval(() => {
      void store.refreshDeviceState().catch(() => undefined);
    }, 2000);
    return () => {
      window.clearInterval(refreshHandle);
      void store.disconnect().catch(() => undefined);
    };
  }, [store]);
  return (
    <DeviceContext.Provider value={store}>
      {children}
    </DeviceContext.Provider>
  );
}

export function useDevice(): {
  store: DeviceStore;
  snapshot: Readonly<DeviceStoreSnapshot>;
} {
  const store = useContext(DeviceContext);
  if (store === null) throw new Error("DeviceProvider is missing");
  const snapshot = useSyncExternalStore(
    (listener) => store.subscribe(listener),
    () => store.snapshot,
    () => store.snapshot
  );
  return { store, snapshot };
}
