cmake_minimum_required(VERSION 3.22)
get_filename_component(folder "${destination}" NAME)
if(NOT EXISTS "${source}/index.html" OR NOT folder STREQUAL "studio-web"
   OR IS_SYMLINK "${destination}" OR IS_SYMLINK "${destination}/assets")
    message(FATAL_ERROR "Invalid Studio web staging paths: ${source} -> ${destination}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory "${source}" "${destination}"
                COMMAND_ERROR_IS_FATAL ANY)

# Vite changes the entry filename whenever its contents change.
file(GLOB entries LIST_DIRECTORIES false RELATIVE "${destination}/assets"
     "${destination}/assets/index-*.js" "${destination}/assets/index-*.css")
foreach(entry IN LISTS entries)
    if(entry MATCHES "^index-[A-Za-z0-9_-]+[.](js|css)$" AND NOT EXISTS "${source}/assets/${entry}")
        file(REMOVE "${destination}/assets/${entry}")
    endif()
endforeach()
