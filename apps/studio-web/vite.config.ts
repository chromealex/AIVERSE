import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Relative asset URLs also work when a signed WKWebView host loads dist/index.html.
export default defineConfig({
  plugins: [react()],
  base: './',
  server: { host: '127.0.0.1', strictPort: true, port: 4173 },
  build: { outDir: 'dist', emptyOutDir: true }
});
