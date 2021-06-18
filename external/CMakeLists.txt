# Supress warning for externals
add_definitions("/w")

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


# spdlog

set(SPDLOG_TIDY OFF CACHE BOOL "spdlog clang-tidy" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "spdlog build shared" FORCE)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)


# protobuf

#TODO: Try find_package first.
message("Using bundled protobuf")
set(WITH_PROTOC ON CACHE BOOL "protobuf build protoc" FORCE)
set(protobuf_BUILD_SHARED_LIBS ON CACHE BOOL "protobuf build shared" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "protobuf build shared" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "protobuf build shared" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "protobuf build shared" FORCE)
add_subdirectory(protobuf/cmake EXCLUDE_FROM_ALL)