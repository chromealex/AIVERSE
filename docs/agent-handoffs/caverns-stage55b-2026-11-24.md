# Caverns Stage55b — Studio TypeScript build fix

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`; starting HEAD `2ee695882194847f2c0c46be56632cb767f51ee8`.

User's Mac Stage55 build reached `npm run build` after the missing web bundle prerequisite was corrected. TypeScript stopped with TS2741 at SceneBrushBar.tsx:7 because the exhaustive Record<BrushKind, string> lacked the existing caverns mode. Add its missing label; keep the dedicated CavernsPanel's context-checked carve/fill/shaft workflow intact. Do not alter the bridge's union or weaken type checking.

Evidence: user supplied TS2741. Source fix committed; typecheck/Vite, native configure/build/CTest and Metal/GI acceptance NOT RUN here. Re-run npm run build before CMake and use existing KINGSTON build. No main merge. CI may supersede the queued prior run due to the new commit.
