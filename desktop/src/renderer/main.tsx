import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { Router } from "wouter";
import { useHashLocation } from "wouter/use-hash-location";

import { DeviceProvider } from "./device-context";
import { AppRouter } from "./router";
import "./styles.css";

async function bootstrap(): Promise<void> {
  const qaMode = new URLSearchParams(window.location.search).get("qa");
  const browserQaBuild =
    import.meta.env.DEV || import.meta.env.VITE_CODEX_BROWSER_QA === "1";
  if (browserQaBuild && qaMode === "device") {
    const { installBrowserQaBridge } = await import("./browser-qa");
    installBrowserQaBridge();
  }

  createRoot(document.getElementById("root")!).render(
    <StrictMode>
      <Router hook={useHashLocation}>
        <DeviceProvider>
          <AppRouter />
        </DeviceProvider>
      </Router>
    </StrictMode>
  );
}

void bootstrap();
