/// <reference types="vite/client" />

import type { CodexMicroBridge } from "../preload";

declare global {
  interface Window {
    codexMicro?: CodexMicroBridge;
  }
}

export {};
