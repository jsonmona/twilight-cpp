# Supress warning for externals
add_definitions("/w")

# cubeb

add_subdirectory(cubeb EXCLUDE_FROM_ALL)


# FFmpeg

if(WIN32)
    # Get a prebuilt package from somewhere and name it ffmpeg-windows-prebuilt
    # I'm using one from https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z
    # Directory structure should look like: bin/*.dll, include/, lib/*.lib
    set(FFMPEG_LIBS avdevice avformat avcodec swresample swscale avutil)
    set(FFMPEG_LIBS ${FFMPEG_LIBS} PARENT_SCOPE)
    
    foreach(LIB IN LISTS FFMPEG_LIBS)
        file(GLOB DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-windows-prebuilt/bin/${LIB}*.dll")
        file(COPY ${DLL_PATH} DESTINATION ${CMAKE_BINARY_DIR}/bin/client/)
        file(COPY ${DLL_PATH} DESTINATION ${CMAKE_BINARY_DIR}/bin/server/)

        # They are in fact shared libraries, but IMPORTED_IMPLIB seems to be broken.
        add_library(${LIB} STATIC IMPORTED GLOBAL)
        set_target_properties(${LIB} PROPERTIES
                IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-windows-prebuilt/lib/${LIB}.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-windows-prebuilt/include/")
    endforeach()
else()
    MESSAGE(FATAL_ERROR "Detecting FFmpeg in other platforms not supported yet.")
endif()


# ImGui

file(GLOB IMGUI_SOURCES "./imgui/*.h" "./imgui/*.cpp")
set(IMGUI_BACKENDS "./imgui/backends/imgui_impl_dx11.h" "./imgui/backends/imgui_impl_dx11.cpp")
add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_BACKENDS})
target_include_directories(imgui PUBLIC ./imgui)


# mbed TLS

set(MBEDTLS_ZLIB_SUPPORT OFF CACHE BOOL "mbedtls zlib support" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "mbedtls enable programs" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "mbedtls treat warning as error" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "mbedtls build tests" FORCE)
add_subdirectory(mbedtls EXCLUDE_FROM_ALL)


# opus

set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "opus build as shared lib" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "opus build tests" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "opus install pkgconfig" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "opus install cmake config" FORCE)
add_subdirectory(opus EXCLUDE_FROM_ALL)


# protobuf

#TODO: Try find_package first.
message("Using bundled protobuf")
set(WITH_PROTOC ON CACHE BOOL "protobuf build protoc" FORCE)
set(protobuf_BUILD_SHARED_LIBS ON CACHE BOOL "protobuf build shared" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "protobuf build shared" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "protobuf build shared" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "protobuf build shared" FORCE)
add_subdirectory(protobuf/cmake EXCLUDE_FROM_ALL)


# spdlog

set(SPDLOG_TIDY OFF CACHE BOOL "spdlog clang-tidy" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "spdlog build shared" FORCE)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)


# toml11

add_library(toml11 INTERFACE)
target_include_directories(toml11 INTERFACE toml11)
