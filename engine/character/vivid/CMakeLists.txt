# VIVID is a pure C++20 presentation module, independent of any genre and RHI.
# Later render/DNA adapters link to it, not the other way round.
add_library(aiverse_vivid STATIC Face.cpp SparseMorph.cpp FaceShapeBinding.cpp)
add_library(AIVERSE::vivid ALIAS aiverse_vivid)
target_include_directories(aiverse_vivid PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(aiverse_vivid PUBLIC cxx_std_20)

# Opt-in sparse morph GPU execution through the cross-platform gfx::Device RHI.
# No direct Metal or Vulkan implementation is linked from this library.
add_library(aiverse_vivid_gpu STATIC GpuMorph.cpp)
add_library(AIVERSE::vivid_gpu ALIAS aiverse_vivid_gpu)
target_link_libraries(aiverse_vivid_gpu PUBLIC aiverse_vivid aiverse_engine)
target_compile_features(aiverse_vivid_gpu PUBLIC cxx_std_20)

# Optional 48B -> 60B GPU ModelVertex stream conversion. Renderer3D still
# requires an explicit opt-in draw-path integration (not enabled by default).
add_library(aiverse_vivid_mesh STATIC GpuMorphMeshStream.cpp)
add_library(AIVERSE::vivid_mesh ALIAS aiverse_vivid_mesh)
target_link_libraries(aiverse_vivid_mesh PUBLIC aiverse_vivid_gpu aiverse_models)
target_compile_features(aiverse_vivid_mesh PUBLIC cxx_std_20)

# Per-character GPU presentation owner. Each actor owns independent writable
# compute/model vertex streams, with no second animation scheduler.
add_library(aiverse_vivid_actor STATIC GpuActorStream.cpp GpuActorRetirement.cpp GpuMorphSharedAsset.cpp GpuMorphAssetCache.cpp)
add_library(AIVERSE::vivid_actor ALIAS aiverse_vivid_actor)
target_link_libraries(aiverse_vivid_actor PUBLIC aiverse_vivid_mesh)
target_compile_features(aiverse_vivid_actor PUBLIC cxx_std_20)

# Versioned human DNA/appearance/rig/groom authoring, independent of
# gameplay ECS and with the same runtime native C++20 asset contract.
add_library(aiverse_vivid_human STATIC
    HumanDNA.cpp HumanRig.cpp HumanMaterials.cpp HumanGroom.cpp
    HumanPreview.cpp HumanCreatorSession.cpp HumanImport.cpp HumanWardrobe.cpp HumanFaceShape.cpp HumanLod.cpp)
add_library(AIVERSE::vivid_human ALIAS aiverse_vivid_human)
target_include_directories(aiverse_vivid_human PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(aiverse_vivid_human PUBLIC aiverse_vivid aiverse_models aiverse_shader_graph)
target_compile_features(aiverse_vivid_human PUBLIC cxx_std_20)

# Legacy render::Pose adapter; core remains usable without aiverse_models.
add_library(aiverse_vivid_render STATIC MorphBinding.cpp)
add_library(AIVERSE::vivid_render ALIAS aiverse_vivid_render)
target_link_libraries(aiverse_vivid_render PUBLIC aiverse_vivid aiverse_models)
target_compile_features(aiverse_vivid_render PUBLIC cxx_std_20)

# MotionWeave owns the complete skeleton/animation graph and presentation ECS.
# VIVID adds only a post-animation facial composition adapter.
add_library(aiverse_vivid_motionweave STATIC MotionWeaveBridge.cpp)
add_library(AIVERSE::vivid_motionweave ALIAS aiverse_vivid_motionweave)
target_link_libraries(aiverse_vivid_motionweave PUBLIC aiverse_vivid_render AIVERSE::MotionWeave)
target_compile_features(aiverse_vivid_motionweave PUBLIC cxx_std_20)


option(AIVERSE_VIVID_SCULPT_BENCH
    "Build optional 10k/100k/1M CPU sculpt picking benchmark (not a CTest)" OFF)
if(AIVERSE_VIVID_SCULPT_BENCH)
    add_executable(aiverse_vivid_sculpt_pick_bench
        ${PROJECT_SOURCE_DIR}/tools/vivid-creator/sculpt_pick_bench.cpp)
    target_link_libraries(aiverse_vivid_sculpt_pick_bench PRIVATE
        aiverse_vivid_human aiverse_zstd_dec)
    target_compile_features(aiverse_vivid_sculpt_pick_bench PRIVATE cxx_std_20)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vivid_human_rig_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/human_rig.cpp)
    target_link_libraries(aiverse_vivid_human_rig_tests PRIVATE aiverse_vivid_human aiverse_zstd_dec)
    add_test(NAME vivid_human_rig COMMAND aiverse_vivid_human_rig_tests)

    add_executable(aiverse_vivid_human_lod_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/human_lod.cpp)
    target_link_libraries(aiverse_vivid_human_lod_tests PRIVATE aiverse_vivid_human aiverse_zstd_dec)
    add_test(NAME vivid_human_lod COMMAND aiverse_vivid_human_lod_tests)

    add_executable(aiverse_vivid_human_appearance_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/human_appearance.cpp)
    target_link_libraries(aiverse_vivid_human_appearance_tests PRIVATE aiverse_vivid_human aiverse_zstd_dec)
    add_test(NAME vivid_human_appearance COMMAND aiverse_vivid_human_appearance_tests)

    add_executable(aiverse_vivid_human_import_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/human_import.cpp)
    target_link_libraries(aiverse_vivid_human_import_tests PRIVATE aiverse_vivid_human aiverse_zstd_dec)
    add_test(NAME vivid_human_import COMMAND aiverse_vivid_human_import_tests)

    add_executable(aiverse_vivid_human_creator_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/human_creator.cpp)
    target_link_libraries(aiverse_vivid_human_creator_tests PRIVATE aiverse_vivid_human aiverse_zstd_dec)
    add_test(NAME vivid_human_creator COMMAND aiverse_vivid_human_creator_tests)

    add_executable(aiverse_vivid_human_dna_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/human_dna.cpp)
    target_link_libraries(aiverse_vivid_human_dna_tests PRIVATE aiverse_vivid_human aiverse_zstd_dec)
    add_test(NAME vivid_human_dna COMMAND aiverse_vivid_human_dna_tests)

    add_executable(aiverse_vivid_face_tests ${PROJECT_SOURCE_DIR}/tests/vivid/face.cpp)
    target_link_libraries(aiverse_vivid_face_tests PRIVATE aiverse_vivid)
    add_test(NAME vivid_face COMMAND aiverse_vivid_face_tests)

    add_executable(aiverse_vivid_sparse_tests ${PROJECT_SOURCE_DIR}/tests/vivid/sparse_morph.cpp)
    target_link_libraries(aiverse_vivid_sparse_tests PRIVATE aiverse_vivid)
    add_test(NAME vivid_sparse_morph COMMAND aiverse_vivid_sparse_tests)

    add_executable(aiverse_vivid_shape_tests ${PROJECT_SOURCE_DIR}/tests/vivid/shape_binding.cpp)
    target_link_libraries(aiverse_vivid_shape_tests PRIVATE aiverse_vivid)
    add_test(NAME vivid_shape_binding COMMAND aiverse_vivid_shape_tests)

    add_executable(aiverse_vivid_gpu_contract_tests ${PROJECT_SOURCE_DIR}/tests/vivid/gpu_contract.cpp)
    target_link_libraries(aiverse_vivid_gpu_contract_tests PRIVATE aiverse_vivid_gpu)
    add_test(NAME vivid_gpu_contract COMMAND aiverse_vivid_gpu_contract_tests)

    add_executable(aiverse_vivid_motionweave_tests ${PROJECT_SOURCE_DIR}/tests/vivid/motionweave_bridge.cpp)
    target_link_libraries(aiverse_vivid_motionweave_tests PRIVATE aiverse_vivid_motionweave aiverse_zstd_dec)
    add_test(NAME vivid_motionweave_bridge COMMAND aiverse_vivid_motionweave_tests)

    # Real Metal execution/readback, separate from the fake-RHI GPU contract.
    # Keep Objective-C++ and Apple frameworks out of non-macOS targets.
    if(AIVERSE_PLATFORM STREQUAL "macos")
        # Root CMake enables OBJCXX before add_subdirectory; never do it here.
        add_executable(aiverse_vivid_metal_gpu_tests ${PROJECT_SOURCE_DIR}/tests/vivid/metal_gpu.mm)
        set_source_files_properties(${PROJECT_SOURCE_DIR}/tests/vivid/metal_gpu.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(aiverse_vivid_metal_gpu_tests PRIVATE
            aiverse_vivid_gpu "-framework Metal" "-framework Foundation")
        # CXX_STANDARD does not consistently select Objective-C++ language mode.
        # metal_gpu.mm includes std::span from the shared C++20 VIVID headers.
        set_target_properties(aiverse_vivid_metal_gpu_tests PROPERTIES
            OBJCXX_STANDARD 20
            OBJCXX_STANDARD_REQUIRED ON
            OBJCXX_EXTENSIONS OFF)
        add_test(NAME vivid_metal_gpu COMMAND aiverse_vivid_metal_gpu_tests)
        set_tests_properties(vivid_metal_gpu PROPERTIES SKIP_RETURN_CODE 77)

        # Real AIVERSE Metal RHI (not direct MTLDevice and not a recording mock).
        # Link the backend source into this small diagnostic executable rather
        # than pulling every editor/platform object from aiverse_runtime.
        add_executable(aiverse_vivid_metal_rhi_tests
            ${PROJECT_SOURCE_DIR}/tests/vivid/metal_rhi.mm
            ${PROJECT_SOURCE_DIR}/engine/gfx/metal/GfxMetal.mm)
        set_source_files_properties(
            ${PROJECT_SOURCE_DIR}/tests/vivid/metal_rhi.mm
            ${PROJECT_SOURCE_DIR}/engine/gfx/metal/GfxMetal.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(aiverse_vivid_metal_rhi_tests PRIVATE
            aiverse_vivid_actor aiverse_vivid_mesh aiverse_vivid_gpu
            "-framework Metal" "-framework QuartzCore" "-framework Foundation")
        set_target_properties(aiverse_vivid_metal_rhi_tests PROPERTIES
            OBJCXX_STANDARD 20
            OBJCXX_STANDARD_REQUIRED ON
            OBJCXX_EXTENSIONS OFF)
        add_test(NAME vivid_metal_rhi COMMAND aiverse_vivid_metal_rhi_tests)
        set_tests_properties(vivid_metal_rhi PROPERTIES SKIP_RETURN_CODE 77)

        # Actual Metal ID/depth raster oracle + async offscreen pixel readback.
        # Uses production gfx::Device; separate from pure source/CPU tests.
        add_executable(aiverse_vivid_metal_sculpt_id_tests
            ${PROJECT_SOURCE_DIR}/tests/vivid/metal_sculpt_id.mm
            ${PROJECT_SOURCE_DIR}/engine/gfx/metal/GfxMetal.mm)
        set_source_files_properties(
            ${PROJECT_SOURCE_DIR}/tests/vivid/metal_sculpt_id.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(aiverse_vivid_metal_sculpt_id_tests PRIVATE
            aiverse_vivid_human aiverse_zstd_dec
            "-framework Metal" "-framework QuartzCore" "-framework Foundation")
        set_target_properties(aiverse_vivid_metal_sculpt_id_tests PROPERTIES
            OBJCXX_STANDARD 20
            OBJCXX_STANDARD_REQUIRED ON
            OBJCXX_EXTENSIONS OFF)
        add_test(NAME vivid_metal_sculpt_id
            COMMAND aiverse_vivid_metal_sculpt_id_tests)
        set_tests_properties(vivid_metal_sculpt_id
            PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    # Whole-character retirement: old Model and GPU actor resources share
    # the same completed-fence lifetime, not just the per-actor output.
    add_executable(aiverse_vivid_human_retirement_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/human_retirement.cpp)
    target_link_libraries(aiverse_vivid_human_retirement_tests PRIVATE
        aiverse_vivid_human aiverse_vivid_actor aiverse_zstd_dec AIVERSE::MotionWeave)
    add_test(NAME vivid_human_retirement
        COMMAND aiverse_vivid_human_retirement_tests)

    # Headless token/failure/idle actor GPU retirement test. Uses the same
    # genre-neutral AsyncResourceRetirementQueue as the renderer.
    # GLES3/WebGL2 CPU fallback: no compute shader or frame serial required.
    add_executable(aiverse_vivid_cpu_actor_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/cpu_actor.cpp)
    target_link_libraries(aiverse_vivid_cpu_actor_tests PRIVATE
        aiverse_vivid_actor aiverse_zstd_dec)
    add_test(NAME vivid_cpu_actor COMMAND aiverse_vivid_cpu_actor_tests)

    add_executable(aiverse_vivid_actor_retirement_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/actor_retirement.cpp)
    target_link_libraries(aiverse_vivid_actor_retirement_tests PRIVATE
        aiverse_vivid_actor aiverse_zstd_dec)
    add_test(NAME vivid_actor_retirement COMMAND aiverse_vivid_actor_retirement_tests)

    add_executable(aiverse_vivid_vertex_override_tests
        ${PROJECT_SOURCE_DIR}/tests/vivid/render_vertex_override.cpp)
    target_link_libraries(aiverse_vivid_vertex_override_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME vivid_vertex_override COMMAND aiverse_vivid_vertex_override_tests)

    add_executable(aiverse_vivid_morph_tests ${PROJECT_SOURCE_DIR}/tests/vivid/morph_binding.cpp)
    target_link_libraries(aiverse_vivid_morph_tests PRIVATE aiverse_vivid_render aiverse_zstd_dec)
    add_test(NAME vivid_morph_binding COMMAND aiverse_vivid_morph_tests)
endif()
