# Called after platform/content/plugin configuration. Generate at configure time and on
# every build, so editing a license alone cannot leave an old notice in a signed app.
set(AIVERSE_PROJECT_NOTICES "" CACHE FILEPATH "Additional game/assets/native-code license notices")
if(NOT AIVERSE_PROJECT_NOTICES AND EXISTS "${AIVERSE_CONTENT_DIR}/THIRD_PARTY_NOTICES.txt")
    set(AIVERSE_PROJECT_NOTICES "${AIVERSE_CONTENT_DIR}/THIRD_PARTY_NOTICES.txt")
endif()
if(NOT AIVERSE_PROJECT_NOTICES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/project-notices.txt")
    set(AIVERSE_PROJECT_NOTICES "${CMAKE_CURRENT_SOURCE_DIR}/project-notices.txt")
endif()
set(AIVERSE_TOOLCHAIN_NOTICES "" CACHE STRING "Additional runtime/toolchain license files")
if(ANDROID AND NOT AIVERSE_TOOLCHAIN_NOTICES)
    foreach(name NOTICE NOTICE.toolchain)
        if(EXISTS "${CMAKE_ANDROID_NDK}/${name}")
            list(APPEND AIVERSE_TOOLCHAIN_NOTICES "${CMAKE_ANDROID_NDK}/${name}")
        endif()
    endforeach()
endif()
set(AIVERSE_NOTICE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(AIVERSE_NOTICE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/notices/THIRD_PARTY_NOTICES.txt")
include("${CMAKE_CURRENT_LIST_DIR}/GenerateNotices.cmake")
add_custom_target(aiverse_notices
    COMMAND "${CMAKE_COMMAND}" "-DAIVERSE_NOTICE_ROOT=${AIVERSE_NOTICE_ROOT}"
        "-DAIVERSE_NOTICE_OUTPUT=${AIVERSE_NOTICE_OUTPUT}"
        "-DAIVERSE_PROJECT_NOTICES=${AIVERSE_PROJECT_NOTICES}"
        "-DAIVERSE_PLUGIN_NOTICES=${AIVERSE_PLUGIN_NOTICES}"
        "-DAIVERSE_TOOLCHAIN_NOTICES=${AIVERSE_TOOLCHAIN_NOTICES}"
        -P "${CMAKE_CURRENT_LIST_DIR}/GenerateNotices.cmake"
    BYPRODUCTS "${AIVERSE_NOTICE_OUTPUT}" VERBATIM)

function(aiverse_package_notices target)
    add_dependencies(${target} aiverse_notices)
    # Resource sources are copied before signing by Xcode; no post-sign bundle mutation.
    if(AIVERSE_PLATFORM MATCHES "^(macos|ios)$")
        target_sources(${target} PRIVATE "${AIVERSE_NOTICE_OUTPUT}")
        set_source_files_properties("${AIVERSE_NOTICE_OUTPUT}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    elseif(NOT AIVERSE_PLATFORM STREQUAL "android")
        add_custom_target(${target}_notices ALL
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${AIVERSE_NOTICE_OUTPUT}"
                "$<TARGET_FILE_DIR:${target}>/THIRD_PARTY_NOTICES.txt"
            DEPENDS aiverse_notices VERBATIM)
        add_dependencies(${target} ${target}_notices)
    endif()
endfunction()
