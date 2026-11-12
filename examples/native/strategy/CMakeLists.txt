aiverse_native_module(ID strategy_demo VERSION 1 ENTRY createStrategyDemo SOURCES Strategy.cpp)
target_link_libraries(aiverse_native_strategy_demo PRIVATE AIVERSE::strategy)
# Build explicitly as an acceptance target, not part of packaged game binaries.
add_executable(aiverse_strategy_native_probe EXCLUDE_FROM_ALL Probe.cpp)
target_link_libraries(aiverse_strategy_native_probe PRIVATE aiverse_game AIVERSE::strategy)
if(NOT MSVC)
    target_compile_options(aiverse_native_strategy_demo PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(aiverse_strategy_native_probe PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
