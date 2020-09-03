cmake_minimum_required(VERSION 3.17)
project(h9_external)

message("***                                 NOTE:                                ***")
message("*** This project needs to be linked to the Max SDK, **NOT** the Max API. ***")
message("*** Please ensure you have downloaded the right SDK.                     ***")

# Fetch the correct verion of the max-api
message(STATUS "Updating Git Submodules")
execute_process(
    COMMAND              git submodule update --init --recursive
    WORKING_DIRECTORY    "${CMAKE_CURRENT_SOURCE_DIR}"
)


# global PATHS, cmake -DMAX_SDK_PATH=../..
set(C74_MAX_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/max-sdk")
set(C74SUPPORT "${C74_MAX_SDK_DIR}/source/c74support")

message("C74_MAX_SDK_DIR=" ${C74_MAX_SDK_DIR})
message("C74SUPPORT=" ${C74SUPPORT})

add_library(${PROJECT_NAME} MODULE h9_external.c)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".mxo")
target_link_libraries(${PROJECT_NAME} PRIVATE libh9)

message("Adding subdir")
add_subdirectory(lib/libh9)
message("Done adding subdir")

# MacOS Specifics (for Xcode project generation with `cmake -G Xcode ..`)
set(MACOSX_DEPLOYMENT_TARGET 10.9)
set(MACOSX_BUNDLE_INFO_PLIST ${C74_MAX_SDK_DIR}/source/Info.plist)

# Build settings and flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${C74SUPPORT}/max-includes/macho-prefix.pch -Wmost -Wno-four-char-constants -Wno-unknown-pragmas -fvisibility=hidden")
file(STRINGS "${C74SUPPORT}/max-includes/c74_linker_flags.txt" C74_SYM_LINKER_FLAGS) 
#set(CMAKE_FRAMEWORK_PATH ${C74SUPPORT}/max-includes;${C74SUPPORT}/msp-includes;${C74SUPPORT}/jit-includes)
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -F ${C74SUPPORT}/max-includes -F ${C74SUPPORT}/msp-includes -F ${C74SUPPORT}/jit-includes -framework MaxAudioAPI -framework JitterAPI ${C74_SYM_LINKER_FLAGS}")

# These I haven't converted from Xcode just yet, still need to figure out how they work in CMake
# set(GCC_PREFIX_HEADER ${C74SUPPORT}/max-includes/macho-prefix.pch)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${C74_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${C74SUPPORT}/max-includes 
    ${C74SUPPORT}/msp-includes
    ${C74SUPPORT}/jit-includes
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libh9/lib
)

