import { defineConfig } from "vite";

export default defineConfig({
    clearScreen: false,
    root: "src",
    build: {
        outDir: "../dist",
        emptyOutDir: true
    },
    server: {
        strictPort: true,
        port: 1420
    }
});