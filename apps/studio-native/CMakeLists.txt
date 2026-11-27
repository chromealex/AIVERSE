# Completely separate, opt-in executable, no ImGui editor source files.
# OBJCXX was enabled by the root CMakeLists before creating targets.
# The sidecar metadata files are authoritative. This SQLite database is a
# regenerable local GUID index; no data is migrated into the game runtime.
# Shared host (StudioHost) + one thin shell per OS: mac/ (Cocoa, WKWebView, Metal), win/ (Win32, WebView2, Vulkan).
# Shared terrain preview headers expose Caverns collision value types.
include_directories("${PROJECT_SOURCE_DIR}/third_party/box3d/include")
set(AIVERSE_STUDIO_SOURCES StudioHost.cpp StudioScene.cpp StudioGameSurface.cpp StudioFacialGpuPreview.cpp
    StudioSession.cpp StudioAssetCatalog.cpp StudioAssetTransfers.cpp)
# GCC's Release optimizer can exceed the memory available for a single
# translation unit in the project Studio build. Keep these large dispatch
# and preview files buildable on an 8 GiB Linux host.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_property(SOURCE StudioHost.cpp StudioScene.cpp StudioSession.cpp APPEND PROPERTY
        COMPILE_OPTIONS "$<$<CONFIG:Release>:-O1;--param=ggc-min-expand=20;--param=ggc-min-heapsize=32768>")
endif()
if(NOT EXISTS "${AIVERSE_STUDIO_WEB_DIST}/index.html")
    message(FATAL_ERROR "HTML5 bundle missing: build apps/studio-web (npm run build) or set AIVERSE_STUDIO_WEB_DIST")
endif()
set(STUDIO_CAPTION_FONT "${PROJECT_SOURCE_DIR}/engine/assets/fonts/ui.ttf" "${PROJECT_SOURCE_DIR}/engine/assets/fonts/LICENSE-ui.txt")
set(STUDIO_NATURE_TEXTURES
    "${PROJECT_SOURCE_DIR}/engine/assets/fx/physis/flame_16x4.png"
    "${PROJECT_SOURCE_DIR}/engine/assets/fx/physis/smoke_8x8.png"
    "${PROJECT_SOURCE_DIR}/engine/assets/fx/physis/LICENSE-fx.txt")
if(WIN32)
    list(APPEND AIVERSE_STUDIO_SOURCES ../editor/EditorSpeech.cpp)
    include(win/StudioWindows.cmake)
elseif(AIVERSE_PLATFORM STREQUAL "linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK_STUDIO REQUIRED IMPORTED_TARGET webkit2gtk-4.1>=2.40)
    find_package(SQLite3 REQUIRED)
    if(NOT TARGET aiverse_gfx_vulkan)
        message(FATAL_ERROR "Linux Studio requires AIVERSE_VULKAN=ON")
    endif()
    add_executable(aiverse_studio linux/Main.cpp ../editor/EditorSpeech.cpp ${AIVERSE_STUDIO_SOURCES})
    target_include_directories(aiverse_studio PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio PRIVATE PkgConfig::GTK_STUDIO aiverse_hosted_client SQLite::SQLite3
        aiverse_lua aiverse_editor_core aiverse_runtime aiverse_fx aiverse_game_view aiverse_gfx_vulkan
        AIVERSE::vivid_human AIVERSE::libraries)
    target_sources(aiverse_studio PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    set_target_properties(aiverse_studio PROPERTIES OUTPUT_NAME AiverseStudio)
    add_custom_target(aiverse_studio_web_assets
        COMMAND "${CMAKE_COMMAND}" "-Dsource=${AIVERSE_STUDIO_WEB_DIST}" "-Ddestination=$<TARGET_FILE_DIR:aiverse_studio>/studio-web" -P "${CMAKE_CURRENT_SOURCE_DIR}/StageWebAssets.cmake"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:aiversemcp>" "$<TARGET_FILE_DIR:aiverse_studio>/$<TARGET_FILE_NAME:aiversemcp>"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:aiverse_studio>/engine/fx/physis" "$<TARGET_FILE_DIR:aiverse_studio>/engine/fonts"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${STUDIO_NATURE_TEXTURES} "$<TARGET_FILE_DIR:aiverse_studio>/engine/fx/physis"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${STUDIO_CAPTION_FONT} "$<TARGET_FILE_DIR:aiverse_studio>/engine/fonts"
        DEPENDS aiversemcp
        VERBATIM)
    add_dependencies(aiverse_studio aiverse_studio_web_assets)
else()
    find_package(SQLite3 REQUIRED)
    add_executable(aiverse_studio MACOSX_BUNDLE mac/Main.mm mac/StudioAssetSchemeHandler.mm ../editor/EditorSpeech_mac.mm ${AIVERSE_STUDIO_SOURCES})
    target_include_directories(aiverse_studio PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio PRIVATE aiverse_hosted_client SQLite::SQLite3 aiverse_lua aiverse_editor_core aiverse_runtime aiverse_fx aiverse_game_view AIVERSE::vivid_human AIVERSE::libraries "-framework Cocoa" "-weak_framework ScreenCaptureKit" "-framework WebKit" "-framework AVFoundation" "-framework Speech")
    target_sources(aiverse_studio PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    set_source_files_properties(mac/Main.mm mac/StudioAssetSchemeHandler.mm ../editor/EditorSpeech_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set(AIVERSE_STUDIO_BUNDLE_ID "com.aiverse.studio.html5" CACHE STRING "Studio application bundle identifier")
    set_target_properties(aiverse_studio PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/mac/StudioInfo.plist.in"
        OUTPUT_NAME AiverseStudioHTML5
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${AIVERSE_STUDIO_BUNDLE_ID}")
    if(EXISTS "${AIVERSE_STUDIO_WEB_DIST}/index.html")
        # Run even when only frontend assets changed. CMake 3.22 uses CMP0112
        # NEW, so TARGET_BUNDLE_CONTENT_DIR does not introduce a reverse dependency.
        add_custom_target(aiverse_studio_web_assets
            DEPENDS "${AIVERSE_STUDIO_WEB_DIST}/index.html"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/MacOS"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:aiversemcp>" "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/MacOS/$<TARGET_FILE_NAME:aiversemcp>"
            COMMAND "${CMAKE_COMMAND}" "-Dsource=${AIVERSE_STUDIO_WEB_DIST}" "-Ddestination=$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/Resources/studio-web" -P "${CMAKE_CURRENT_SOURCE_DIR}/StageWebAssets.cmake"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/Resources/engine/fx/physis"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${STUDIO_NATURE_TEXTURES} "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/Resources/engine/fx/physis"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/Resources/engine/fonts"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${STUDIO_CAPTION_FONT} "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/Resources/engine/fonts"
            VERBATIM)
        add_dependencies(aiverse_studio aiverse_studio_web_assets)
    else()
        message(FATAL_ERROR "HTML5 bundle missing: build on KINGSTON and set AIVERSE_STUDIO_WEB_DIST")
    endif()
endif()
if(TARGET aiverse_studio)
    if(TARGET aiversebuild)
        add_custom_target(aiverse_studio_build_tool
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:aiverse_studio>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:aiversebuild>"
                "$<TARGET_FILE_DIR:aiverse_studio>/$<TARGET_FILE_NAME:aiversebuild>"
            DEPENDS aiversebuild
            VERBATIM)
        add_dependencies(aiverse_studio aiverse_studio_build_tool)
    endif()
    if(TARGET aiverse_client)
        target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_GAME_CLIENT="$<TARGET_FILE:aiverse_client>")
        add_dependencies(aiverse_studio aiverse_client)
        if(AIVERSE_PLATFORM STREQUAL "linux")
            add_custom_target(aiverse_studio_game_player
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:aiverse_client>"
                    "$<TARGET_FILE_DIR:aiverse_studio>/aiverse_client"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory "${AIVERSE_ENGINE_ASSETS}"
                    "$<TARGET_FILE_DIR:aiverse_studio>/content/engine"
                DEPENDS aiverse_client
                VERBATIM)
            add_dependencies(aiverse_studio aiverse_studio_game_player)
        endif()
    endif()
    target_sources(aiverse_studio PRIVATE ../fps/client.cpp)
    target_compile_definitions(aiverse_studio PRIVATE AIVERSE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio PRIVATE aiverse_ui aiverse_authority aiverse_render2d aiverse_roads_view AIVERSE::MotionWeave AIVERSE::View)
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
    add_executable(aiverse_studio_building_topology_tests tests/StudioBuildingTopologyTests.cpp)
    target_include_directories(aiverse_studio_building_topology_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_building_topology_tests PRIVATE aiverse_editor_core AIVERSE::libraries)
    add_test(NAME studio_building_topology COMMAND aiverse_studio_building_topology_tests "${PROJECT_SOURCE_DIR}")
    set_tests_properties(studio_building_topology PROPERTIES LABELS "EDITOR")
    add_executable(aiverse_studio_building_preview_tests tests/StudioBuildingPreviewTests.cpp)
    target_include_directories(aiverse_studio_building_preview_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_building_preview_tests PRIVATE aiverse_editor_core aiverse_runtime aiverse_game_view AIVERSE::libraries "-framework Cocoa")
    target_sources(aiverse_studio_building_preview_tests PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    add_test(NAME studio_building_preview COMMAND aiverse_studio_building_preview_tests "${PROJECT_SOURCE_DIR}")
    set_tests_properties(studio_building_preview PROPERTIES LABELS "EDITOR;GPU;Metal" TIMEOUT 90)
    add_executable(aiverse_studio_metal_lighting_tests tests/StudioMetalLightingTests.mm StudioScene.cpp StudioFacialGpuPreview.cpp StudioAssetCatalog.cpp)
    target_include_directories(aiverse_studio_metal_lighting_tests PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/third_party/box3d/include")
    target_link_libraries(aiverse_studio_metal_lighting_tests PRIVATE aiverse_ui aiverse_audio SQLite::SQLite3 aiverse_editor_core aiverse_runtime aiverse_fx aiverse_game_view AIVERSE::libraries "-framework Cocoa")
    target_sources(aiverse_studio_metal_lighting_tests PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    add_test(NAME studio_metal_lighting COMMAND aiverse_studio_metal_lighting_tests "${CMAKE_CURRENT_BINARY_DIR}/metal-lighting-captures")
    set_tests_properties(studio_metal_lighting PROPERTIES LABELS "EDITOR;LIGHTING" TIMEOUT 90 PASS_REGULAR_EXPRESSION "Studio Metal lighting PASS" FAIL_REGULAR_EXPRESSION "Studio Metal lighting FAIL")
    # Real GPU SpectraFX preview: the Scene owns all device work and renders
    # into its normal depth-tested HDR pass; no browser/ImGui fake frames.
    add_executable(aiverse_studio_spectrafx_preview_tests
        tests/StudioSpectraPreviewTests.mm StudioScene.cpp
        StudioFacialGpuPreview.cpp StudioAssetCatalog.cpp)
    target_include_directories(aiverse_studio_spectrafx_preview_tests
        PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/third_party/box3d/include")
    target_link_libraries(aiverse_studio_spectrafx_preview_tests PRIVATE aiverse_ui aiverse_audio
        SQLite::SQLite3 aiverse_editor_core aiverse_runtime aiverse_fx
        aiverse_game_view AIVERSE::vivid_human AIVERSE::libraries "-framework Cocoa")
    target_sources(aiverse_studio_spectrafx_preview_tests
        PRIVATE $<TARGET_OBJECTS:aiverse_runtime>)
    set_source_files_properties(tests/StudioSpectraPreviewTests.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    add_test(NAME studio_spectrafx_preview
        COMMAND aiverse_studio_spectrafx_preview_tests)
    add_test(NAME studio_spectrafx_fire_smoke
        COMMAND aiverse_studio_spectrafx_preview_tests)
    set_tests_properties(studio_spectrafx_fire_smoke PROPERTIES
        ENVIRONMENT "AIVERSE_SPECTRA_FIRE_GRAPH_PATH=${PROJECT_SOURCE_DIR}/examples/spectrafx/fire_smoke_v2.vfx.json;AIVERSE_SPECTRA_ENGINE_ASSETS=${PROJECT_SOURCE_DIR}/engine/assets/"
        LABELS "EDITOR;SpectraFX;GPU;Volumetrics" TIMEOUT 120
        PASS_REGULAR_EXPRESSION "Studio SpectraFX Metal preview PASS"
        FAIL_REGULAR_EXPRESSION "Studio SpectraFX Metal preview FAIL")
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
    target_include_directories(aiverse_studio_session_tests PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/third_party/box3d/include")
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
    add_executable(aiverse_studio_spectra_transfer_tests tests/StudioSpectraTransferLeaseTests.cpp)
    target_include_directories(aiverse_studio_spectra_transfer_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    add_test(NAME studio_spectra_transfer COMMAND aiverse_studio_spectra_transfer_tests)
    set_tests_properties(studio_spectra_transfer PROPERTIES LABELS "EDITOR;SpectraFX")
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
    set_tests_properties(studio_session PROPERTIES LABELS "EDITOR" ENVIRONMENT "AIVERSE_AGENT_LINK_DIR=${CMAKE_CURRENT_BINARY_DIR}/session-test-agent-link")
endif()

# Keep the dependency-light history contract in the ordinary CTest inventory.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_studio_wizard_dictation_tests tests/StudioWizardDictationTests.cpp)
    target_include_directories(aiverse_studio_wizard_dictation_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_wizard_dictation_tests PRIVATE aiverse_engine)
    add_test(NAME studio_wizard_dictation COMMAND aiverse_studio_wizard_dictation_tests)
    add_executable(aiverse_studio_owner_history_tests tests/StudioOwnerHistoryTests.cpp)
    target_include_directories(aiverse_studio_owner_history_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_owner_history_tests PRIVATE aiverse_engine aiverse_pack)
    add_test(NAME studio_owner_history COMMAND aiverse_studio_owner_history_tests)
    set_tests_properties(studio_owner_history PROPERTIES LABELS "EDITOR")
    if(Python3_Interpreter_FOUND)
        if(APPLE)
            set(studio_distribution "$<TARGET_BUNDLE_DIR:aiverse_studio>")
        else()
            set(studio_distribution "$<TARGET_FILE_DIR:aiverse_studio>")
        endif()
        add_test(NAME studio_standalone_bundle
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/standalone_bundle.py"
                "${studio_distribution}")
        set_tests_properties(studio_standalone_bundle PROPERTIES LABELS "EDITOR;MCP")
        add_test(NAME studio_wizard_art_download
            COMMAND "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/tests/wizard_art_download.py")
    endif()
    if(TARGET aiversemcp AND Python3_Interpreter_FOUND)
        add_test(NAME studio_mcp_registry
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/mcp/audit_registry.py"
                "$<TARGET_FILE:aiversemcp>" --engine "${PROJECT_SOURCE_DIR}")
        set_tests_properties(studio_mcp_registry PROPERTIES LABELS "EDITOR;MCP" TIMEOUT 90)
    endif()
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

if(AIVERSE_BUILD_TESTS)
    get_property(studio_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    list(FILTER studio_test_targets INCLUDE REGEX "^aiverse_studio_.*_tests$")
    add_custom_target(aiverse_studio_verification DEPENDS aiverse_studio aiversemcp ${studio_test_targets})
endif()
