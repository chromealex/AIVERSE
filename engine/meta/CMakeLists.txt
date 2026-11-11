# Can be consumed directly by any project's native C++ target.
add_library(aiverse_meta STATIC Meta.cpp MetaData.cpp Leaderboard.cpp Chat.cpp Service.cpp ServiceData.cpp
    LiveOps.cpp LiveOpsData.cpp LiveOpsRewards.cpp LiveOpsActivities.cpp LiveOpsRetention.cpp
    LiveOpsCompetition.cpp LiveOpsMatchmaking.cpp LiveOpsRatings.cpp Rating.cpp)
add_library(aiverse::meta ALIAS aiverse_meta)
target_include_directories(aiverse_meta PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../..")
target_compile_features(aiverse_meta PUBLIC cxx_std_20)
set_target_properties(aiverse_meta PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(TARGET aiverse_engine)
    target_link_libraries(aiverse_meta PUBLIC aiverse_engine)
else()
    target_sources(aiverse_meta PRIVATE ../data/Json.cpp ../core/Fixed.cpp)
endif()

# aiverse_native_module uses TARGET_OBJECTS, so propagate its link dependency explicitly.
function(aiverse_native_use_meta native_target)
    if(NOT TARGET ${native_target} OR NOT TARGET aiverse_game)
        message(FATAL_ERROR "aiverse_native_use_meta expects a registered native target in AIVERSE")
    endif()
    target_link_libraries(${native_target} PRIVATE aiverse::meta)
    target_link_libraries(aiverse_game PUBLIC aiverse::meta)
endfunction()
