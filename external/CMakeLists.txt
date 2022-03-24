# Supress warning for externals
add_definitions("/w")


# cubeb

add_subdirectory(cubeb EXCLUDE_FROM_ALL)


# FFmpeg

if(WIN32)
    # I have included a "patent-free" build of ffmpeg (just including libvpx, vp8, vp9 and matroska).
    # Will shortly find a way to build that using CMake ExternalProject.
    # Note: I'm not a lawyer and I'm not sure if it really is patent-free.
    #       However, at least it doesn't contain any MPEG-LA related codecs in it.

    if(NOT(CMAKE_SIZEOF_VOID_P EQUAL 8))
        message(FATAL_ERROR "Only 64-bit ffmpeg prebuilt package is provided")
    endif()

    file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-win64-prebuilt)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-win64-prebuilt.tar.gz
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/
    )

    file(GLOB FFMPEG_DLLS "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-win64-prebuilt/bin/*.dll")
    file(COPY ${FFMPEG_DLLS} DESTINATION "${CMAKE_BINARY_DIR}/bin/server/")
    file(COPY ${FFMPEG_DLLS} DESTINATION "${CMAKE_BINARY_DIR}/bin/client/")

    set(FFMPEG_LIBS avcodec avformat swresample swscale avutil)
    set(FFMPEG_LIBS ${FFMPEG_LIBS} PARENT_SCOPE)
    
    foreach(LIB IN LISTS FFMPEG_LIBS)
        # They are in fact shared libraries, but IMPORTED_IMPLIB seems to be broken.
        add_library(${LIB} STATIC IMPORTED GLOBAL)
        set_target_properties(${LIB} PROPERTIES
                IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-win64-prebuilt/bin/${LIB}.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-win64-prebuilt/include/")
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


# OpenH264

add_library(openh264 INTERFACE)
set_target_properties(openh264 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/
)


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
