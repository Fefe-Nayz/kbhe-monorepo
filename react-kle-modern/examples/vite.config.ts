import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

const examplesDir = fileURLToPath(new URL(".", import.meta.url));
const projectRoot = resolve(examplesDir, "..");

export default defineConfig({
  root: examplesDir,
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      "@": resolve(examplesDir, "src"),
    },
  },
  server: {
    fs: {
      allow: [projectRoot],
    },
  },
});
