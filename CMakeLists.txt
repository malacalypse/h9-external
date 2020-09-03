cmake_minimum_required(VERSION 3.17)
project(h9_external)

message("")
message("***                                 NOTE:                                ***")
message("*** This project needs to be linked to the Max SDK, **NOT** the Max API. ***")
message("*** Please ensure you have downloaded the right SDK.                     ***")
message("")

# Fetch the correct verion of the max-api
message(STATUS "Updating Git Submodules")
execute_process(
    COMMAND              git submodule update --init --recursive
    WORKING_DIRECTORY    "${CMAKE_CURRENT_SOURCE_DIR}"
)


# global PATHS, cmake -DMAX_SDK_PATH=../..
#set(C74_MAX_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/max-sdk")
find_path(C74_MAX_SDK_DIR
    c74support
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../..
    DOC "Max SDK Source Directory"
    REQUIRED)
set(C74SUPPORT "${C74_MAX_SDK_DIR}/c74support")

message("C74_MAX_SDK_DIR=" ${C74_MAX_SDK_DIR})
message("C74SUPPORT=" ${C74SUPPORT})

add_library(${PROJECT_NAME} MODULE h9_external.c)
target_link_libraries(${PROJECT_NAME} PRIVATE libh9)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

### Output ###
if (APPLE)
    find_library(JITTER_LIBRARY "JitterAPI" HINTS "${C74SUPPORT}/jit-includes"  )
    target_link_libraries(${PROJECT_NAME} PUBLIC ${JITTER_LIBRARY})

    set_property(TARGET ${PROJECT_NAME}
                 PROPERTY BUNDLE True)
    set_property(TARGET ${PROJECT_NAME}
                 PROPERTY BUNDLE_EXTENSION "mxo")
    set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_WRAPPER_EXTENSION "mxo")
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION "${GIT_VERSION_TAG}")
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)
elseif (WIN32)
    if ("${PROJECT_NAME}" MATCHES "_test")
    else ()

        target_link_libraries(${PROJECT_NAME} PUBLIC ${MaxAPI_LIB})
        target_link_libraries(${PROJECT_NAME} PUBLIC ${MaxAudio_LIB})
        target_link_libraries(${PROJECT_NAME} PUBLIC ${Jitter_LIB})
    endif ()

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".mxe64")
    else ()
        set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".mxe")
    endif ()

    # warning about constexpr not being const in c++14
    set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/wd4814")

    # do not generate ILK files
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/INCREMENTAL:NO")
endif ()

message("Adding subdir")
add_subdirectory(lib/libh9)
message("Done adding subdir")

# MacOS Specifics (for Xcode project generation with `cmake -G Xcode ..`)
set(MACOSX_DEPLOYMENT_TARGET 10.9)

# Build settings and flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${C74SUPPORT}/max-includes/macho-prefix.pch -Wmost -Wno-four-char-constants -Wno-unknown-pragmas -fvisibility=hidden")
file(STRINGS "${C74SUPPORT}/max-includes/c74_linker_flags.txt" C74_SYM_LINKER_FLAGS)
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

