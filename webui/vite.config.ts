/// <reference types="vitest/config" />
import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

// `npm run dev` proxies the API to a real badge (PIXELDESK_HOST overrides the name).
const badge = `http://${process.env.PIXELDESK_HOST ?? 'pixeldesk.local'}`;

export default defineConfig({
  // One self-contained index.html: the firmware embeds and serves a single file.
  plugins: [viteSingleFile()],
  build: { outDir: 'dist', emptyOutDir: true },
  server: { proxy: { '/api': badge, '/wifi': badge } },
  test: { environment: 'jsdom' },
});
