cmake_minimum_required(VERSION 3.20)
project(RobloxEspCPP LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Common)

# libESP - Shared Library (dylib)
set(LIBESPMANAGER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libESPManager/src)

add_library(ESPManager SHARED
    ${LIBESPMANAGER_SRC_DIR}/esp_manager.cpp
    ${LIBESPMANAGER_SRC_DIR}/objc_shims.mm
)

set_target_properties(ESPManager PROPERTIES
    PREFIX "lib"
    SUFFIX ".dylib"
    OUTPUT_NAME "ESPManager"
)

target_include_directories(ESPManager PRIVATE
    ${LIBESPMANAGER_SRC_DIR}
    ${COMMON_DIR}
)

find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
find_library(UNIFORMTYPEIDENTIFIERS_FRAMEWORK UniformTypeIdentifiers REQUIRED)
find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit REQUIRED)
find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
find_library(COREIMAGE_FRAMEWORK CoreImage REQUIRED)

target_link_libraries(ESPManager PRIVATE
    ${FOUNDATION_FRAMEWORK}
    ${COCOA_FRAMEWORK}
    ${COREGRAPHICS_FRAMEWORK}
    ${WEBKIT_FRAMEWORK}
    ${UNIFORMTYPEIDENTIFIERS_FRAMEWORK}
    ${SCREENCAPTUREKIT_FRAMEWORK}
    ${COREMEDIA_FRAMEWORK}
    ${COREIMAGE_FRAMEWORK}
)

target_compile_options(ESPManager PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Injector - Executable
set(INJECTOR_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Injector/src)

add_executable(injector
    ${INJECTOR_SRC_DIR}/main.cpp
)

target_include_directories(injector PRIVATE
    ${COMMON_DIR}
    ${INJECTOR_SRC_DIR}
)

target_compile_options(injector PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

set(INJECTOR_ENTITLEMENTS ${CMAKE_CURRENT_SOURCE_DIR}/injector/debug.entitlements)

add_custom_command(
    TARGET injector
    POST_BUILD
    COMMAND codesign
        --force
        --sign -
        --entitlements ${INJECTOR_ENTITLEMENTS}
        --timestamp=none
        $<TARGET_FILE:injector>
    COMMENT "Codesigning injector with debug entitlements"
)