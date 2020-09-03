cmake_minimum_required(VERSION 3.0)
project(h9_external)

# Fetch the correct verion of libh9
message(STATUS "Updating Git Submodules")
execute_process(
    COMMAND              git submodule update --init --recursive
    WORKING_DIRECTORY    "${CMAKE_CURRENT_SOURCE_DIR}"
)

find_path(MAX_API_SCRIPTS
    max-pretarget.cmake
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../.. ~
    PATH_SUFFIXES script max-api/script
    DOC "Max API Source Directory"
    REQUIRED)
set(MAX_API_ROOT "${MAX_API_SCRIPTS}/..")
message("Found Max API at " ${MAX_API_ROOT})

include(${MAX_API_ROOT}/script/max-pretarget.cmake)

include_directories(
    "${C74_INCLUDES}"
)

add_library(${PROJECT_NAME} MODULE ${PROJECT_NAME}.cpp )
add_subdirectory(lib/libh9)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/libh9/lib)
target_link_libraries(${PROJECT_NAME} PRIVATE libh9)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

include(${MAX_API_ROOT}/script/max-posttarget.cmake)
