#!/usr/bin/env bash
# VIVID one-pass native acceptance: fetch + configure + build + CTest +
# actual macOS Renderer3D/MotionWeave screenshot verification.
# Run: bash tools/vivid-validate.sh
# Use --no-gui for an unattended environment, --full for all engine tests.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRANCH="${AIVERSE_VIVID_REF:-feature/vivid-digital-humans}"
BUILD="${AIVERSE_VIVID_BUILD_DIR:-$ROOT/build/vivid-onepass}"
JOBS="${AIVERSE_VIVID_JOBS:-4}"
GUI=1
FULL=0
for option in "$@"; do
    case "$option" in
        --no-gui) GUI=0 ;;
        --full) FULL=1 ;;
        *) printf 'Unknown option: %s\n' "$option" >&2; exit 2 ;;
    esac
done
if [[ "$(uname -s)" != Darwin ]]; then
    printf 'One-pass acceptance requires macOS Metal. Use Linux CI for portable subsets.\n' >&2
    exit 2
fi
# Keep unrelated worktrees completely untouched. Do not switch a dirty tree.
if [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]]; then
    printf 'Refusing checkout over modified tracked files: %s\n' "$ROOT" >&2
    git -C "$ROOT" status --short >&2
    exit 2
fi
git -C "$ROOT" fetch origin "$BRANCH"
git -C "$ROOT" switch --detach "origin/$BRANCH"
HEAD="$(git -C "$ROOT" rev-parse HEAD)"
mkdir -p "$BUILD"
LOGDIR="$BUILD/vivid-validation"
mkdir -p "$LOGDIR"
printf 'VIVID validation SHA: %s\n' "$HEAD" | tee "$LOGDIR/head.txt"
printf 'Root: %s\nBuild: %s\nLogs: %s\n' "$ROOT" "$BUILD" "$LOGDIR"
trap 'code=$?; printf "\nVIVID one-pass FAILED (exit=%s). Logs: %s\n" "$code" "$LOGDIR" >&2' ERR

run_logged() {
    local label="$1"
    shift
    printf '\n===== %s =====\n' "$label"
    "$@" 2>&1 | tee "$LOGDIR/$label.log"
}
# Run lightweight CPU fixture checks in the same final acceptance sweep.
run_logged optical-python-tests python3 -m unittest discover \
    -s "$ROOT/tools/vivid-creator" -p "test_optical_ab.py"
run_logged texture-overlap-python-tests python3 -m unittest discover \
    -s "$ROOT/tools/vivid-creator" -p "test_texture_rhi_overlap.py"
run_logged cmake-configure cmake -S "$ROOT" -B "$BUILD" \
    -DAIVERSE_BUILD_TESTS=ON

if [[ "$FULL" -eq 1 ]]; then
    run_logged cmake-build cmake --build "$BUILD" -j "$JOBS"
else
    run_logged cmake-build cmake --build "$BUILD" --target \
        aiverse_vivid_face_tests \
        aiverse_vivid_sparse_tests \
        aiverse_vivid_shape_tests \
        aiverse_vivid_morph_tests \
        aiverse_vivid_gpu_contract_tests \
        aiverse_vivid_metal_gpu_tests \
        aiverse_vivid_metal_rhi_tests \
        aiverse_vivid_metal_sculpt_id_tests \
        aiverse_vivid_vertex_override_tests \
        aiverse_vivid_actor_retirement_tests \
        aiverse_vivid_cpu_actor_tests \
        aiverse_vivid_human_dna_tests \
        aiverse_vivid_human_rig_tests \
        aiverse_vivid_human_appearance_tests \
        aiverse_vivid_human_import_tests \
        aiverse_vivid_human_creator_tests \
        aiverse_vivid_creator \
        aiverse_vivid_motionweave_tests \
        aiverse_motionweave_api_tests \
        aiverse_vivid_renderer_preview \
        -j "$JOBS"
fi

if [[ "$FULL" -eq 1 ]]; then
    run_logged ctest ctest --test-dir "$BUILD" --output-on-failure -j "$JOBS"
else
    # Deterministic sequential GPU tests; build *every* selected target first.
    run_logged ctest ctest --test-dir "$BUILD" --output-on-failure \
        -R '^(vivid_|motionweave_api$)' -j 1
fi

if [[ "$GUI" -eq 1 ]]; then
    PREVIEW="$BUILD/aiverse_vivid_renderer_preview"
    if [[ ! -x "$PREVIEW" ]]; then
        printf 'Missing GUI preview executable: %s\n' "$PREVIEW" >&2
        exit 1
    fi
    PREFIX="$LOGDIR/vivid"
    run_logged renderer-preview env AIVERSE_VIVID_PREVIEW_PREFIX="$PREFIX" "$PREVIEW"
    for stage in baseline morph motionweave; do
        output="$PREFIX-$stage.ppm"
        if [[ ! -s "$output" ]]; then
            printf 'Missing/empty preview screenshot: %s\n' "$output" >&2
            exit 1
        fi
    done
    if ! grep -q 'pass=1' "$LOGDIR/renderer-preview.log"; then
        printf 'Renderer3D/MotionWeave preview did not report pass=1.\n' >&2
        exit 1
    fi
    # 7AX: one-actor Metal smoke must report both asynchronous
    # *same-frame* colour screenshot / 60B GPU actor ID pixel probes.
    # The 12-actor crowd mode intentionally does not run these single-
    # actor provenance probes.
    for parity_stage in 1 2; do
        if ! grep -q "GPU colour/ID coverage stage=${parity_stage} .*depth_scene=.*depth_id=.*delta=.* PASS" \
            "$LOGDIR/renderer-preview.log"; then
            printf 'VIVID Metal colour/ID parity stage %s is missing.\n' \
                "$parity_stage" >&2
            exit 1
        fi
    done

    # Stage 7AZ: separate native Renderer3D scene with TWO independently
    # GPU-written actor sources. One selected VIVID head + one sampled
    # alpha-mask occluder are both submitted through normal colour/depth
    # and are then observed in the independent zero-ID mask/depth pass.
    MASK_PREFIX="$LOGDIR/vivid-mask-parity"
    run_logged renderer-mask-parity env \
        AIVERSE_VIVID_MASK_PARITY=1 \
        AIVERSE_VIVID_PREVIEW_PREFIX="$MASK_PREFIX" "$PREVIEW"
    for stage in baseline morph motionweave; do
        if [[ ! -s "$MASK_PREFIX-$stage.ppm" ]]; then
            printf 'VIVID 7AZ real colour screenshot missing: %s\n' \
                "$stage" >&2
            exit 1
        fi
    done
    if ! grep -q 'pass=1' "$LOGDIR/renderer-mask-parity.log" || \
       ! grep -q 'GPU colour/ID coverage stage=1 .*mask=alpha-hole PASS' \
            "$LOGDIR/renderer-mask-parity.log" || \
       ! grep -q 'GPU colour/ID coverage stage=2 .*mask=solid-occlusion PASS' \
            "$LOGDIR/renderer-mask-parity.log"; then
        printf 'VIVID 7AZ same-frame two-actor GPU Mask scene-depth parity failed.\n' >&2
        exit 1
    fi

    # Stage 7BA: third independent GPU-compute-written actor; two real
    # Renderer3D Mask colour submissions and one same-frame GPU ID/depth
    # pass with distinct zero-ID mask inputs.
    MULTI_MASK_PREFIX="$LOGDIR/vivid-multi-mask"
    run_logged renderer-multi-mask-parity env \
        AIVERSE_VIVID_MASK_PARITY=2 \
        AIVERSE_VIVID_PREVIEW_PREFIX="$MULTI_MASK_PREFIX" "$PREVIEW"
    for stage in baseline morph motionweave; do
        if [[ ! -s "$MULTI_MASK_PREFIX-$stage.ppm" ]]; then
            printf 'VIVID 7BA real multi-Mask screenshot missing: %s\n' \
                "$stage" >&2
            exit 1
        fi
    done
    if ! grep -q 'pass=1' "$LOGDIR/renderer-multi-mask-parity.log" || \
       ! grep -q 'GPU mask sources=2' \
           "$LOGDIR/renderer-multi-mask-parity.log" || \
       ! grep -q 'GPU colour/ID coverage stage=1 .*mask=alpha-hole PASS' \
           "$LOGDIR/renderer-multi-mask-parity.log" || \
       ! grep -q 'GPU colour/ID coverage stage=2 .*mask=solid-occlusion PASS' \
           "$LOGDIR/renderer-multi-mask-parity.log"; then
        printf 'VIVID 7BA two independent GPU Mask renderer/depth tests failed.\n' >&2
        exit 1
    fi

    # A second REAL scene validates 12 GPU actors sharing the same immutable
    # morph data and shader pipelines while producing distinct face streams.
    CROWD="${AIVERSE_VIVID_CROWD_ACTORS:-12}"
    CROWD_PREFIX="$LOGDIR/vivid-crowd"
    run_logged renderer-crowd-preview env \
        AIVERSE_VIVID_PREVIEW_ACTORS="$CROWD" \
        AIVERSE_VIVID_PREVIEW_PREFIX="$CROWD_PREFIX" "$PREVIEW"
    for stage in baseline morph motionweave; do
        output="$CROWD_PREFIX-$stage.ppm"
        if [[ ! -s "$output" ]]; then
            printf 'Missing/empty crowd screenshot: %s\n' "$output" >&2
            exit 1
        fi
    done
    if ! grep -q 'pass=1' "$LOGDIR/renderer-crowd-preview.log"; then
        printf 'VIVID multi-actor Renderer3D/MotionWeave preview did not pass.\n' >&2
        exit 1
    fi

    # Build and execute the real ImGui Creator shader/material preview in
    # noninteractive capture mode. Tests skin, sclera, iris, cornea, hair
    # cards, user-facing renderer and screenshot data on a Metal GPU.
    CREATOR="$BUILD/aiverse_vivid_creator"
    if [[ ! -x "$CREATOR" ]]; then
        printf 'Missing VIVID Creator executable: %s\n' "$CREATOR" >&2
        exit 1
    fi
    CREATOR_PPM="$LOGDIR/vivid-creator.ppm"
    run_logged creator-gui-preview env \
        AIVERSE_VIVID_CREATOR_CAPTURE="$CREATOR_PPM" \
        "$CREATOR" --smoke
    if [[ ! -s "$CREATOR_PPM" ]]; then
        printf 'VIVID Creator real skin/eye/groom screenshot missing.\n' >&2
        exit 1
    fi
    if ! grep -q 'pass=1' "$LOGDIR/creator-gui-preview.log"; then
        printf 'VIVID Creator Metal preview did not report pass=1.\n' >&2
        exit 1
    fi

    # Stage 7AF: source-compatible lower-cost material presets still build
    # and render on the native Metal Creator path. Hardware GPU comparisons
    # and Vulkan/GLES acceptance remain separate checks.
    for material_tier in low medium; do
        material_ppm="$LOGDIR/vivid-material-$material_tier.ppm"
        rm -f "$material_ppm"
        if [[ "$material_tier" == low ]]; then
            run_logged "creator-material-$material_tier" env \
                AIVERSE_VIVID_CREATOR_CAPTURE="$material_ppm" \
                "$CREATOR" --smoke --presentation-config \
                "$ROOT/tools/vivid-creator/vivid-presentation.mobile.json"
        else
            run_logged "creator-material-$material_tier" env \
                AIVERSE_VIVID_CREATOR_CAPTURE="$material_ppm" \
                "$CREATOR" --smoke --material-quality "$material_tier"
        fi
        if [[ ! -s "$material_ppm" ]] || \
           ! grep -q 'pass=1' "$LOGDIR/creator-material-$material_tier.log"; then
            printf 'VIVID material quality capture failed: %s\n' "$material_tier" >&2
            exit 1
        fi
    done

    CPU_PPM="$LOGDIR/vivid-creator-cpu.ppm"
    run_logged creator-cpu-preview env \
        AIVERSE_VIVID_CREATOR_CAPTURE="$CPU_PPM" \
        "$CREATOR" --smoke --cpu-morph
    if [[ ! -s "$CPU_PPM" ]] || \
       ! grep -q 'actor=CPU .*pass=1' "$LOGDIR/creator-cpu-preview.log"; then
        printf 'VIVID Creator CPU morph capture failed.\n' >&2
        exit 1
    fi

    # Check that changing Skin/Eye DNA affects actual 3D viewport pixels,
    # not just Inspector text. Use a separate GUI run with the warm preset.
    WARM_PPM="$LOGDIR/vivid-creator-warm.ppm"
    run_logged creator-warm-preview env \
        AIVERSE_VIVID_CREATOR_CAPTURE="$WARM_PPM" \
        "$CREATOR" --smoke --preset warm
    if [[ ! -s "$WARM_PPM" ]]; then
        printf 'VIVID warm-skin Creator GPU screenshot missing.\n' >&2
        exit 1
    fi
    run_logged creator-material-ab python3 \
        "$ROOT/tools/vivid-creator/compare_ppm.py" \
        "$CREATOR_PPM" "$WARM_PPM"

    # Machine-readable viewport RGB diagnostic; source-level BRDF checks
    # cannot replace a captured, nonempty GPU material A/B comparison.
    run_logged creator-optical-ab python3 \
        "$ROOT/tools/vivid-creator/optical_ab.py" \
        "$CREATOR_PPM" "$WARM_PPM" \
        --output "$LOGDIR/creator-optical-ab.json"

    # Stage 7AE: a fixed neutral procedural model, camera and light, with
    # MotionWeave/hair animation paused. Exactly ONE DNA scalar varies
    # between low and high screenshots for each optical material sweep.
    # ROI fractions are provisional until calibrated against real captures.
    for optical_case in reference \
        skin-scatter-low skin-scatter-high \
        eye-ior-low eye-ior-high \
        hair-rough-low hair-rough-high; do
        optical_ppm="$LOGDIR/optical-$optical_case.ppm"
        rm -f "$optical_ppm"
        run_logged "creator-optical-$optical_case" env \
            AIVERSE_VIVID_CREATOR_CAPTURE="$optical_ppm" \
            "$CREATOR" --smoke --optical-case "$optical_case"
        if [[ ! -s "$optical_ppm" ]] || \
           ! grep -q 'pass=1' "$LOGDIR/creator-optical-$optical_case.log"; then
            printf 'VIVID optical case failed: %s\n' "$optical_case" >&2
            exit 1
        fi
    done
    run_logged creator-optical-skin-ab python3 \
        "$ROOT/tools/vivid-creator/optical_ab.py" \
        "$LOGDIR/optical-skin-scatter-low.ppm" \
        "$LOGDIR/optical-skin-scatter-high.ppm" \
        --label skin-scattering --parameter skin.scatteringMm \
        --reference-value 0.10 --candidate-value 4.0 \
        --roi 0.20,0.16,0.81,0.91 \
        --threshold 4 --min-changed 1 \
        --output "$LOGDIR/optical-skin-scattering.json"
    run_logged creator-optical-eye-ab python3 \
        "$ROOT/tools/vivid-creator/optical_ab.py" \
        "$LOGDIR/optical-eye-ior-low.ppm" \
        "$LOGDIR/optical-eye-ior-high.ppm" \
        --label cornea-ior --parameter eyes.corneaIor \
        --reference-value 1.25 --candidate-value 1.45 \
        --roi 0.25,0.20,0.75,0.70 \
        --threshold 4 --min-changed 1 \
        --output "$LOGDIR/optical-cornea-ior.json"
    run_logged creator-optical-hair-ab python3 \
        "$ROOT/tools/vivid-creator/optical_ab.py" \
        "$LOGDIR/optical-hair-rough-low.ppm" \
        "$LOGDIR/optical-hair-rough-high.ppm" \
        --label hair-roughness --parameter groom.roughness \
        --reference-value 0.08 --candidate-value 0.90 \
        --roi 0.15,0.04,0.85,0.62 \
        --threshold 4 --min-changed 1 \
        --output "$LOGDIR/optical-hair-roughness.json"

    # Exercise the OPEN external OBJ+manifest+named morph importer in a
    # real window. No bundled copyrighted scanned human assets required.
    IMPORT_PPM="$LOGDIR/vivid-imported-head.ppm"
    run_logged creator-import-preview env \
        AIVERSE_VIVID_CREATOR_CAPTURE="$IMPORT_PPM" \
        "$CREATOR" --smoke --asset "$ROOT/tests/vivid/assets/import-head.manifest.json"
    if [[ ! -s "$IMPORT_PPM" ]]; then
        printf 'VIVID imported-head Metal PPM missing/empty.\n' >&2
        exit 1
    fi
    if ! grep -q 'pass=1' "$LOGDIR/creator-import-preview.log"; then
        printf 'VIVID Creator external OBJ/shape import did not render.\n' >&2
        exit 1
    fi
else
    printf 'GUI preview intentionally skipped by --no-gui; this is NOT full visual acceptance.\n'
fi

{
    printf 'SHA=%s\n' "$HEAD"
    printf 'GPU+MotionWeave CTest=PASS\n'
    if [[ "$GUI" -eq 1 ]]; then
        printf 'Renderer3D colour/shadow/MotionWeave visual preview=PASS\n'
        printf 'Renderer3D shared GPU asset crowd preview=PASS\n'
        printf 'VIVID Creator skin/eye/groom interactive GUI GPU preview=PASS\n'
        printf 'VIVID Creator neutral-vs-warm actual material pixel A/B=PASS\n'
        printf 'VIVID Creator quantitative optical A/B report=PASS\n'
        printf 'VIVID isolated optical DNA captures and regional A/B=PASS\n'
        printf 'VIVID Low/Medium native material quality preview=PASS\n'

        printf 'VIVID Creator external OBJ/DNA/morph import GUI preview=PASS\n'
    else
        printf 'Renderer3D visual preview=NOT RUN (--no-gui)\n'
    fi
    printf 'Logs=%s\n' "$LOGDIR"
} | tee "$LOGDIR/summary.txt"
printf '\nVIVID one-pass verification COMPLETED.\n'
