# Caverns compiler-smoke tests independent of optional RHI and Box3D.
# These targets intentionally cover only pure CPU contracts. Integration
# tests for GeometryScene, Studio and physics require full native QA.
foreach(cave_test IN ITEMS
        terraweave_caverns_mesh_test
        terraweave_caverns_swap_policy_test
        terraweave_caverns_owner_barrier_test
        terraweave_caverns_preview_pacer_test)
    add_executable(aiverse_${cave_test}
        ${PROJECT_SOURCE_DIR}/tests/${cave_test}.cpp)
    target_include_directories(aiverse_${cave_test} PRIVATE
        ${PROJECT_SOURCE_DIR})
    target_compile_features(aiverse_${cave_test} PRIVATE cxx_std_20)
    aiverse_keep_test_asserts(aiverse_${cave_test})
    add_test(NAME ${cave_test} COMMAND aiverse_${cave_test})
    set_tests_properties(${cave_test} PROPERTIES LABELS "TerraWeave;Caverns;CPU")
endforeach()
