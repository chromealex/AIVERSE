# Completely separate, opt-in executable, no ImGui editor source files.
# OBJCXX was enabled by the root CMakeLists before creating targets.
# The sidecar metadata files are authoritative. This SQLite database is a
# regenerable local GUID index; no data is migrated into the game runtime.
# Shared host (StudioHost) + one thin shell per OS: mac/ (Cocoa, WKWebView, Metal), win/ (Win32, WebView2, Vulkan).
set(AIVERSE_STUDIO_SOURCES StudioHost.cpp StudioScene.cpp StudioGameSurface.cpp StudioFacialGpuPreview.cpp
    StudioSession.cpp StudioAssetCatalog.cpp StudioAssetTransfers.cpp)
set(AIVERSE_STUDIO_WEB_DIST "${PROJECT_SOURCE_DIR}/apps/studio-web/dist" CACHE PATH "Built Studio web assets")
if(NOT EXISTS "${AIVERSE_STUDIO_WEB_DIST}/index.html")
    message(FATAL_ERROR "HTML5 bundle missing: build apps/studio-web (npm run build) or set AIVERSE_STUDIO_WEB_DIST")
endif()
if(WIN32)
    include(win/StudioWindows.cmake)
else()
    find_package(SQLite3 REQUIRED)
    add_executable(aiverse_studio MACOSX_BUNDLE mac/Main.mm mac/StudioAssetSchemeHandler.mm ${AIVERSE_STUDIO_SOURCES})
    target_include_directories(aiverse_studio PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio PRIVATE aiverse_hosted_client SQLite::SQLite3 aiverse_lua aiverse_editor_core aiverse_runtime aiverse_fx aiverse_game_view AIVERSE::vivid_human AIVERSE::libraries "-framework Cocoa" "-framework WebKit")
    target_sources(aiverse_studio PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    # Publish / Build panel: the packager built next to Studio.
    if(TARGET aiversebuild)
        add_dependencies(aiverse_studio aiversebuild)
        target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_BUILD_TOOL="$<TARGET_FILE:aiversebuild>")
    endif()
    # Agent panel: the MCP server the in-editor Claude Code run talks to (forwards to this Studio's port).
    if(TARGET aiversemcp)
        add_dependencies(aiverse_studio aiversemcp)
        target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_MCP_TOOL="$<TARGET_FILE:aiversemcp>")
    endif()
    set_source_files_properties(mac/Main.mm mac/StudioAssetSchemeHandler.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(aiverse_studio PROPERTIES
        OUTPUT_NAME AiverseStudioHTML5
        MACOSX_BUNDLE_GUI_IDENTIFIER com.aiverse.studio.html5)
    if(EXISTS "${AIVERSE_STUDIO_WEB_DIST}/index.html")
        # Run even when only frontend assets changed. CMake 3.22 uses CMP0112
        # NEW, so TARGET_BUNDLE_CONTENT_DIR does not introduce a reverse dependency.
        add_custom_target(aiverse_studio_web_assets
            DEPENDS "${AIVERSE_STUDIO_WEB_DIST}/index.html"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${AIVERSE_STUDIO_WEB_DIST}"
                "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/Resources/studio-web"
            VERBATIM)
        add_dependencies(aiverse_studio aiverse_studio_web_assets)
    else()
        message(FATAL_ERROR "HTML5 bundle missing: build on KINGSTON and set AIVERSE_STUDIO_WEB_DIST")
    endif()
endif()
# Portable Studio image codec contract: compile the actual private stb
# implementation using diagnostics as errors, not a globally disabled -W
# category. Exercise real lossless PNG and lossy JPEG memory roundtrips.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_studio_image_codecs_tests
        tests/StudioImageCodecsWarningTests.cpp)
    target_include_directories(aiverse_studio_image_codecs_tests
        PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_options(aiverse_studio_image_codecs_tests PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Werror=unused-function>
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Werror=missing-field-initializers>
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Werror=deprecated-declarations>)
    add_test(NAME studio_image_codecs COMMAND aiverse_studio_image_codecs_tests)
    set_tests_properties(studio_image_codecs PROPERTIES LABELS "EDITOR;TOOLS"
        PASS_REGULAR_EXPRESSION "Studio image codecs PASS"
        FAIL_REGULAR_EXPRESSION "Studio image codecs FAIL")
endif()

if(AIVERSE_BUILD_TESTS AND APPLE)
    add_executable(aiverse_studio_metal_lighting_tests tests/StudioMetalLightingTests.mm StudioScene.cpp StudioFacialGpuPreview.cpp StudioAssetCatalog.cpp)
    target_include_directories(aiverse_studio_metal_lighting_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_metal_lighting_tests PRIVATE SQLite::SQLite3 aiverse_editor_core aiverse_runtime aiverse_fx aiverse_game_view AIVERSE::libraries "-framework Cocoa")
    target_sources(aiverse_studio_metal_lighting_tests PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    add_test(NAME studio_metal_lighting COMMAND aiverse_studio_metal_lighting_tests "${CMAKE_CURRENT_BINARY_DIR}/metal-lighting-captures")
    set_tests_properties(studio_metal_lighting PROPERTIES LABELS "EDITOR;LIGHTING" TIMEOUT 90 PASS_REGULAR_EXPRESSION "Studio Metal lighting PASS" FAIL_REGULAR_EXPRESSION "Studio Metal lighting FAIL")
    # Real GPU SpectraFX preview: the Scene owns all device work and renders
    # into its normal depth-tested HDR pass; no browser/ImGui fake frames.
    add_executable(aiverse_studio_spectrafx_preview_tests
        tests/StudioSpectraPreviewTests.mm StudioScene.cpp
        StudioFacialGpuPreview.cpp StudioAssetCatalog.cpp)
    target_include_directories(aiverse_studio_spectrafx_preview_tests
        PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_spectrafx_preview_tests PRIVATE
        SQLite::SQLite3 aiverse_editor_core aiverse_runtime aiverse_fx
        aiverse_game_view AIVERSE::vivid_human AIVERSE::libraries "-framework Cocoa")
    target_sources(aiverse_studio_spectrafx_preview_tests
        PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    set_source_files_properties(tests/StudioSpectraPreviewTests.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    add_test(NAME studio_spectrafx_preview
        COMMAND aiverse_studio_spectrafx_preview_tests)
    set_tests_properties(studio_spectrafx_preview PROPERTIES
        LABELS "EDITOR;SpectraFX;GPU" TIMEOUT 90
        PASS_REGULAR_EXPRESSION "Studio SpectraFX Metal preview PASS"
        FAIL_REGULAR_EXPRESSION "Studio SpectraFX Metal preview FAIL")
    add_executable(aiverse_studio_render_lighting_tests tests/StudioRenderLightingTests.cpp StudioSession.cpp StudioAssetCatalog.cpp StudioAssetTransfers.cpp)
    target_include_directories(aiverse_studio_render_lighting_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_render_lighting_tests PRIVATE aiverse_lua aiverse_editor_core SQLite::SQLite3)
    add_test(NAME studio_render_lighting COMMAND aiverse_studio_render_lighting_tests "${CMAKE_CURRENT_BINARY_DIR}/render-lighting-fixture")
    set_tests_properties(studio_render_lighting PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_human_appearance_session_tests tests/StudioHumanLookSessionTests.cpp StudioSession.cpp StudioAssetCatalog.cpp StudioAssetTransfers.cpp)
    target_include_directories(aiverse_studio_human_appearance_session_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_human_appearance_session_tests PRIVATE aiverse_lua aiverse_editor_core SQLite::SQLite3)
    add_test(NAME studio_human_appearance_session COMMAND aiverse_studio_human_appearance_session_tests)
    set_tests_properties(studio_human_appearance_session PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_human_creator_tests tests/StudioHumanCreatorTests.cpp)
    target_include_directories(aiverse_studio_human_creator_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_human_creator_tests PRIVATE aiverse_zstd_dec)
    add_test(NAME studio_human_creator COMMAND aiverse_studio_human_creator_tests)
    set_tests_properties(studio_human_creator PROPERTIES LABELS "EDITOR" PASS_REGULAR_EXPRESSION "studio human creator PASS")
    add_executable(aiverse_studio_content_tests tests/StudioContentDirectoryTests.cpp)
    target_include_directories(aiverse_studio_content_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_content_directory COMMAND aiverse_studio_content_tests)
    set_tests_properties(studio_content_directory PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_asset_transfer_tests tests/StudioAssetTransferTests.cpp StudioAssetCatalog.cpp StudioAssetTransfers.cpp)
    target_include_directories(aiverse_studio_asset_transfer_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_asset_transfer_tests PRIVATE SQLite::SQLite3 aiverse_editor_core)
    add_test(NAME studio_asset_transfers COMMAND aiverse_studio_asset_transfer_tests)
    set_tests_properties(studio_asset_transfers PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_move_journal_tests tests/StudioMoveJournalTests.cpp)
    target_include_directories(aiverse_studio_move_journal_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_move_journal_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_move_journal COMMAND aiverse_studio_move_journal_tests)
    set_tests_properties(studio_move_journal PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_asset_catalog_tests tests/StudioAssetCatalogTests.cpp StudioAssetCatalog.cpp)
    target_include_directories(aiverse_studio_asset_catalog_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_asset_catalog_tests PRIVATE SQLite::SQLite3 aiverse_editor_core)
    add_test(NAME studio_asset_catalog COMMAND aiverse_studio_asset_catalog_tests)
    set_tests_properties(studio_asset_catalog PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_session_tests tests/StudioSessionTests.cpp StudioSession.cpp StudioAssetCatalog.cpp StudioAssetTransfers.cpp)
    target_include_directories(aiverse_studio_session_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_session_tests PRIVATE aiverse_lua aiverse_editor_core SQLite::SQLite3)
    add_executable(aiverse_studio_fps_lighting_contract_tests tests/StudioFpsLightingContractTests.cpp)
    target_include_directories(aiverse_studio_fps_lighting_contract_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_fps_lighting_contract COMMAND aiverse_studio_fps_lighting_contract_tests)
    set_tests_properties(studio_fps_lighting_contract PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_fps_lighting_document_tests tests/StudioFpsLightingDocumentTests.cpp)
    target_include_directories(aiverse_studio_fps_lighting_document_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_fps_lighting_document_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_fps_lighting_document COMMAND aiverse_studio_fps_lighting_document_tests)
    set_tests_properties(studio_fps_lighting_document PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_light_preview_math_tests tests/StudioLightPreviewMathTests.cpp)
    target_include_directories(aiverse_studio_light_preview_math_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_light_preview_math COMMAND aiverse_studio_light_preview_math_tests)
    set_tests_properties(studio_light_preview_math PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_authored_lights_tests tests/StudioAuthoredLightsTests.cpp)
    target_include_directories(aiverse_studio_authored_lights_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_authored_lights_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_authored_lights COMMAND aiverse_studio_authored_lights_tests)
    set_tests_properties(studio_authored_lights PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_pbr_adapter_tests tests/StudioPbrMaterialPreviewTests.cpp)
    target_include_directories(aiverse_studio_pbr_adapter_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_pbr_adapter_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_pbr_adapter COMMAND aiverse_studio_pbr_adapter_tests)
    set_tests_properties(studio_pbr_adapter PROPERTIES LABELS "EDITOR")
    # Project Asset GUID + typed facial input bridge; GPU acceptance is
    # tested separately from this portable binding contract.
    add_executable(aiverse_studio_facial_preview_binding_tests
        tests/StudioFacialPreviewBindingTests.cpp StudioAssetCatalog.cpp)
    target_include_directories(aiverse_studio_facial_preview_binding_tests
        PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_facial_preview_binding_tests
        PRIVATE SQLite::SQLite3 aiverse_editor_core AIVERSE::vivid_human)
    add_test(NAME studio_facial_preview_binding
        COMMAND aiverse_studio_facial_preview_binding_tests)
    set_tests_properties(studio_facial_preview_binding PROPERTIES LABELS "EDITOR")

    # Stage 185: compiler/CPU proof of the real imported-model semantic
    # MotionWeave facial layer, including 52 channels and existing eye IK.
    add_executable(aiverse_studio_facial_runtime_tests
        tests/StudioFacialRuntimeTests.cpp)
    target_include_directories(aiverse_studio_facial_runtime_tests
        PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_facial_runtime_tests
        PRIVATE aiverse_models aiverse_zstd_dec AIVERSE::libraries)
    add_test(NAME studio_facial_runtime
        COMMAND aiverse_studio_facial_runtime_tests)
    set_tests_properties(studio_facial_runtime PROPERTIES LABELS "EDITOR")

    add_executable(aiverse_studio_facial_gpu_tests
        tests/StudioFacialGpuTests.mm StudioFacialGpuPreview.cpp StudioAssetCatalog.cpp)
    target_include_directories(aiverse_studio_facial_gpu_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_facial_gpu_tests PRIVATE
        SQLite::SQLite3 aiverse_editor_core aiverse_runtime AIVERSE::vivid_human AIVERSE::libraries
        "-framework Cocoa" "-framework QuartzCore" "-framework Metal")
    set_source_files_properties(tests/StudioFacialGpuTests.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    add_test(NAME studio_facial_gpu COMMAND aiverse_studio_facial_gpu_tests)
    set_tests_properties(studio_facial_gpu PROPERTIES LABELS "EDITOR;GPU" TIMEOUT 60)

    # Facial Control Rig authoring is project-local and native validated.
    add_executable(aiverse_studio_facial_rig_tests tests/StudioFacialRigDocumentTests.cpp)
    target_include_directories(aiverse_studio_facial_rig_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_facial_rig_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_facial_rig_document COMMAND aiverse_studio_facial_rig_tests)
    set_tests_properties(studio_facial_rig_document PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_shader_graph_draft_tests tests/StudioShaderGraphDraftTests.cpp)
    target_include_directories(aiverse_studio_shader_graph_draft_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_shader_graph_draft_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_shader_graph_draft COMMAND aiverse_studio_shader_graph_draft_tests)
    set_tests_properties(studio_shader_graph_draft PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_shader_graph_name_tests tests/StudioShaderGraphValidationTests.cpp)
    target_include_directories(aiverse_studio_shader_graph_name_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_shader_graph_name COMMAND aiverse_studio_shader_graph_name_tests)
    set_tests_properties(studio_shader_graph_name PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_material_validation_tests tests/StudioMaterialValidationTests.cpp)
    target_include_directories(aiverse_studio_material_validation_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_material_validation COMMAND aiverse_studio_material_validation_tests)
    find_program(AIVERSE_STUDIO_NODE node)
    if(AIVERSE_STUDIO_NODE)
        add_test(NAME studio_bridge COMMAND "${AIVERSE_STUDIO_NODE}" --experimental-strip-types --test
            "${PROJECT_SOURCE_DIR}/apps/studio-web/tests/bridge.test.mjs")
        add_test(NAME studio_material_schema
            COMMAND "${AIVERSE_STUDIO_NODE}" --experimental-strip-types --test
            "${PROJECT_SOURCE_DIR}/apps/studio-web/tests/materialSchema.test.mjs")
    endif()
    set_tests_properties(studio_material_validation PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_lighting_validation_tests tests/StudioLightingValidationTests.cpp)
    target_include_directories(aiverse_studio_lighting_validation_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_lighting_validation COMMAND aiverse_studio_lighting_validation_tests)
    set_tests_properties(studio_lighting_validation PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_model_picking_tests tests/StudioModelPickingTests.cpp)
    target_include_directories(aiverse_studio_model_picking_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_model_picking_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_model_picking COMMAND aiverse_studio_model_picking_tests)
    set_tests_properties(studio_model_picking PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_visual_picking_tests tests/StudioVisualPickingTests.cpp)
    target_include_directories(aiverse_studio_visual_picking_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_visual_picking_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_visual_picking COMMAND aiverse_studio_visual_picking_tests)
    set_tests_properties(studio_visual_picking PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_document_visuals_tests tests/StudioDocumentVisualsTests.cpp)
    target_include_directories(aiverse_studio_document_visuals_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_document_visuals_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_document_visuals COMMAND aiverse_studio_document_visuals_tests)
    set_tests_properties(studio_document_visuals PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_scene_nodes_tests tests/StudioSceneNodesTests.cpp)
    target_include_directories(aiverse_studio_scene_nodes_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_scene_nodes_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_scene_nodes COMMAND aiverse_studio_scene_nodes_tests)
    set_tests_properties(studio_scene_nodes PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_terrain_tests tests/StudioTerrainGeometryTests.cpp)
    target_include_directories(aiverse_studio_terrain_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_terrain_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_terrain_geometry COMMAND aiverse_studio_terrain_tests)
    set_tests_properties(studio_terrain_geometry PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_viewport_layout_tests tests/StudioViewportLayoutTests.cpp)
    target_include_directories(aiverse_studio_viewport_layout_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_viewport_layout COMMAND aiverse_studio_viewport_layout_tests)
    set_tests_properties(studio_viewport_layout PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_render_thread_tests tests/StudioRenderThreadTests.cpp)
    target_include_directories(aiverse_studio_render_thread_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    find_package(Threads REQUIRED)
    target_link_libraries(aiverse_studio_render_thread_tests PRIVATE Threads::Threads)
    add_test(NAME studio_render_thread COMMAND aiverse_studio_render_thread_tests)
    set_tests_properties(studio_render_thread PROPERTIES LABELS "EDITOR")
    add_test(NAME studio_session COMMAND aiverse_studio_session_tests)
    set_tests_properties(studio_session PROPERTIES LABELS "EDITOR")
endif()

foreach(target aiverse_studio aiverse_studio_session_tests aiverse_studio_human_appearance_session_tests aiverse_studio_human_creator_tests
        aiverse_studio_render_lighting_tests aiverse_studio_metal_lighting_tests)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE aiverse_vivid_human aiverse_modeling)
    endif()
endforeach()
if(APPLE)
    add_executable(aiverse_vivid_assemble ../../tools/vivid-creator/assemble.cpp)
    target_include_directories(aiverse_vivid_assemble PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_vivid_assemble PRIVATE aiverse_vivid_human aiverse_modeling aiverse_zstd_dec)
endif()
