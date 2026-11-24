# Stage 70 — runtime identity and native file IO

Branch feature/spectrafx-html5-data-interfaces-20261123. Commits 824bd9d562d37b538b4855f2ee154b772eadcef3, f1ec00b4a3b453435c072f34cf87c75e3a59aaf6, f4ec93127caefcab9b3f8efbaa4e5c41b96ac2e6.

GraphRuntime derives serialized graph/override/options identity and wraps SPFR bytes. Native file IO helper bounded at 1 MiB. No tests/builds. Identity >4096 bytes disables restore; build/asset fingerprint missing; write is non-atomic and trusted native paths only. Studio/MCP file transfer and timeline metadata remain. Next implement bounded browser transfer, trusted identity digest and clock envelope. Not merged.
