# FFmpeg

if(WIN32)
	# FFmpeg 4.4 (f68ab9de4e) prebuilt library
	# Obtained from https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z
	# Plan is to compile this myself. But for now, this should be enough.
	set(FFMPEG_LIBS avcodec avdevice avformat avutil swscale swresample)
	set(FFMPEG_LIBS ${FFMPEG_LIBS} PARENT_SCOPE)
	
	foreach(LIB IN LISTS FFMPEG_LIBS)
		file(GLOB DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-windows-prebuilt/bin/${LIB}*.dll")
		file(COPY ${DLL_PATH} DESTINATION ${CMAKE_BINARY_DIR}/src/)

		# They are in fact shared libraries, but IMPORTED_IMPLIB seems to be broken.
		add_library(${LIB} STATIC IMPORTED GLOBAL)
		set_target_properties(${LIB} PROPERTIES
				IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-windows-prebuilt/lib/avcodec.lib"
				INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-windows-prebuilt/include/")
	endforeach()
else()
    MESSAGE(FATAL_ERROR "Detecting FFmpeg in other platforms not supported yet.")
endif()