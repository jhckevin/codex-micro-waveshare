import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

export default defineConfig({
  base: "./",
  plugins: [react(), tailwindcss()],
  root: ".",
  build: {
    outDir: "dist-renderer",
    emptyOutDir: true
  },
  test: {
    environment: "node",
    include: ["src/**/*.test.ts"]
  }
});
