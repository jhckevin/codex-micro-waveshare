# Source-only Windows client

Use a project-local Node environment and `npm ci`, `npm test`, `npm run build`.
Native helpers require the Windows SDK/compiler; see existing build helper sources. macOS is not validated.
No update service/domain is configured by default. `CODEX_MICRO_UPDATE_BASE_URL` can be supplied by a self-hosting developer (HTTPS only); this does not enable encrypted update RPC in the source-only firmware.
Electron distribution publishing is disabled. Build locally; no release artifact is supplied.
