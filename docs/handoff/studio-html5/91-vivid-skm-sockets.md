# Stage 91 — Arbitrary imported SKM attachment controls

Added named socket authoring, arbitrary slot names, separate/shared skeleton
selection and keep-world/snap rules to existing HumanWardrobeControls using Studio
UI components/tokens. Native HumanWardrobe assembles the socket node hierarchy
and independent child rigs; StudioSession normalizes optional socket transforms
on recipe loading. No new bridge methods or browser file writes.

See docs/vivid-skm-attachments.md for behavior, compatibility and limitations.
Changes take effect through native Assemble/Show result, not an immediate gizmo.

Actually executed, all outputs/caches on KINGSTON:
- CMake aiverse_studio, aiverse_vivid_human_wardrobe_tests,
  aiverse_studio_human_appearance_session_tests PASS; build-skm-final.log.
- Scoped CTest vivid_human_wardrobe + studio_human_appearance_session PASS2/2;
  native/test-reports/20261120-175028-57514, test-skm-final.log.
- npm run typecheck; npm run test:web (existing75); incremental TypeScript with
  KINGSTON tsBuildInfoFile; Vite build PASS. Final typecheck/Vite repeated after
  bone-selection adjustment. Final dist copied to KINGSTON native app bundle.
- Python UI contracts PASS29; test-skm-ui.log. git diff --check PASS.
- tools/studio/verify_mac.sh NOT RUN because it hardcodes an internal build path
  contrary to the user's KINGSTON-only instruction. Relevant native/web checks
  above used existing KINGSTON build instead. UI screenshots/interaction NOT RUN.
- No CI/push, no new visual attachment/video acceptance claim.

Important correction to previous motion report: the user observed hair not
following the head. Prior limited frames do not establish attachment correctness;
that earlier conclusion is withdrawn. This stage implements the attachment
mechanism and numerical regression, but does not repair Ada's existing hair mesh
or claim its skinning is now visually correct. Next: extract/identify the actual
hair rig root, attach to head through the explicit model path, and test exaggerated
head rotation/tilt with hair and lashes in native video. Exact code SHA appears in
the separately committed VIVID/integration handoffs.
