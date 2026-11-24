# Stage 78 — replay UTF-8 size contract

Branch `feature/spectrafx-html5-data-interfaces-20261123`. Implementation `0249eeed626faad280c2c104fab3d6866a941dcd` uses TextEncoder byte length rather than UTF-16 code units to enforce the same 64 KiB limit as native file import/export. Regression added to the already registered spectraViewportModel suite: exact 65536-byte multibyte JSON passes; oversized multibyte JSON with fewer than 65536 UTF-16 units is rejected. No format or runtime changes.

Tests authored, not executed in this stage. PR #115 is closed without merge; previous HEAD CI was queued. Next run npm run test:graphs and native acceptance at the new HEAD, inspect remaining CI failures. Main untouched.
