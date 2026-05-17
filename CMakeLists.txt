# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    set(SDKCONFIG_DEFAULTS
        "sdkconfig.defaults"
        "sdkconfig.defaults.local")
endif()

set(HAPANEL_VERSION_BASE "0.1.0")
execute_process(
    COMMAND git -c safe.directory=${CMAKE_CURRENT_LIST_DIR} describe --tags --always --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE HAPANEL_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE HAPANEL_GIT_VERSION_RESULT)
if(HAPANEL_GIT_VERSION_RESULT EQUAL 0 AND NOT HAPANEL_GIT_VERSION STREQUAL "")
    set(PROJECT_VER "${HAPANEL_VERSION_BASE}+${HAPANEL_GIT_VERSION}")
else()
    set(PROJECT_VER "${HAPANEL_VERSION_BASE}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)
project(hapanel)
