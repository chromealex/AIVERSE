# Stage 64 — tagged GPU capture

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 8a702bcfc42ddf8ccd58908788f4684824de4100, 57474f18ed2ed13406181d4f96263c9874c7e964, 6185570d66093ae2416df0e30f8f38bc5248a15a, ca369b556565f032499538e18fde2e94f6ed2bce.

Gfx.h adds tagged capture virtual with unknown serial fallback; GfxMetal.mm tags actual encoded drawable frame and checks completion status; StudioScene.h/StudioHost.cpp propagate serial. Skill updated. No tests/builds. frameExact remains false: request-time tick/camera still need matching per-frame metadata. Next implement bounded per-frame provenance ring, match callback serial and audit capture allocation/queue failure handling.
