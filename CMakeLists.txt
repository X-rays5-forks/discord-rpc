cmake_minimum_required (VERSION 3.2.0)
project (DiscordRPC)

include(get_cpm.cmake)
include(GNUInstallDirs)

option(BUILD_EXAMPLES "Build example apps" ON)

# format
file(GLOB_RECURSE ALL_SOURCE_FILES
    examples/*.cpp examples/*.h examples/*.c
    include/*.h
    src/*.cpp src/*.h src/*.c
)

# thirdparty stuff
execute_process(
    COMMAND mkdir ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ERROR_QUIET
)

# add subdirs

add_subdirectory(src)
if (BUILD_EXAMPLES)
    add_subdirectory(examples/send-presence)
endif(BUILD_EXAMPLES)
