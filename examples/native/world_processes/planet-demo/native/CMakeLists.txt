aiverse_native_module(ID world_processes VERSION 1 ENTRY createWorldProcessesModule SOURCES WorldProcessesModule.cpp)
target_link_libraries(aiverse_native_world_processes PRIVATE AIVERSE::libraries)

# Optional physics remains a native-project dependency, never an engine-wide dependency.
if(TARGET AIVERSE::physics)
    target_compile_definitions(aiverse_native_world_processes PRIVATE AIVERSE_PLANET_FOREST_PHYSICS_HOST=1)
    target_link_libraries(aiverse_native_world_processes PRIVATE AIVERSE::physics)
    target_link_libraries(aiverse_native_modules INTERFACE AIVERSE::physics)
endif()
