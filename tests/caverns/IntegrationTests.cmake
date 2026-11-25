# Existing full-native Caverns fixtures. Included after the owning targets
# are defined, never from the portable smoke-test directory.
function(aiverse_caverns_native_fixture suffix labels)
    set(test_name terraweave_caverns_${suffix}_test)
    add_executable(aiverse_${test_name} ${PROJECT_SOURCE_DIR}/tests/${test_name}.cpp)
    target_link_libraries(aiverse_${test_name} PRIVATE ${ARGN})
    aiverse_keep_test_asserts(aiverse_${test_name})
    add_test(NAME ${test_name} COMMAND aiverse_${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "TerraWeave;Caverns;${labels}")
endfunction()

foreach(suffix IN ITEMS edit_runtime editor_visual_preview multinav surface_cut)
    aiverse_caverns_native_fixture(${suffix} "CPU;ECS" aiverse_engine aiverse_world Threads::Threads)
endforeach()

foreach(suffix IN ITEMS batch_geometry edit_presentation geometry_scene mixed_shard presentation resident shard_gpu_host stage6)
    aiverse_caverns_native_fixture(${suffix} "CPU;Renderer3D" aiverse_models aiverse_world aiverse_zstd_dec Threads::Threads)
endforeach()

if(AIVERSE_ENABLE_PHYSICS AND TARGET AIVERSE::physics)
foreach(suffix IN ITEMS authored_nav_stage box3d empty_tile shadow_owners volume_collision_nav)
    aiverse_caverns_native_fixture(${suffix} "PHYSICS" aiverse_models aiverse_world AIVERSE::physics)
endforeach()
endif()

if(TARGET aiverse_editor_core)
foreach(suffix IN ITEMS editor)
    aiverse_caverns_native_fixture(${suffix} "EDITOR;MCP" aiverse_editor_core aiverse_zstd_dec)
endforeach()
endif()
