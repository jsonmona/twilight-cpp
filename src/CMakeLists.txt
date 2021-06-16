set(COMMON_WINDOWS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/platform/windows/ComWrapper.h
)

set(SERVER_WINDOWS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/stdafx.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/ColorConvD3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/ColorConvD3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/MediaFoundationEncoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/MediaFoundationEncoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/StreamManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/StreamManager.cpp
)

add_library(common STATIC ${COMMON_WINDOWS_SRC})

add_executable(server WIN32 ${SERVER_WINDOWS_SRC})
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server PUBLIC
	common ${FFMPEG_LIBS}
	"dxgi.lib" "d3d11.lib"
	"mfuuid.lib" "mfplat.lib"
	"ws2_32.lib"
)
target_compile_definitions(server PUBLIC "-DUNICODE" "-D_UNICODE")

# Compile HLSL shader
if(WIN32)
    set(FXC_FILES)

    find_program(FXC fxc DOC "fx compiler")
    if(NOT FXC)
        message(FATAL_ERROR "Cannot find fxc.")
    endif(NOT FXC)

    macro(FN_ADD_SHADER ENTRY PROFILE FILE)
        get_filename_component(HLSL_PATH ${FILE} ABSOLUTE)
        get_filename_component(FILE_WE ${FILE} NAME_WE)
        add_custom_command(OUTPUT ${FILE_WE}-${ENTRY}.fxc
                           COMMAND ${FXC} /nologo /E "${ENTRY}" /T "${PROFILE}" /Fo "${FILE_WE}-${ENTRY}.fxc" ${HLSL_PATH}
                           MAIN_DEPENDENCY ${FILE}
                           COMMENT "Compiling HLSL shader: ${FILE} for ${ENTRY}"
                           WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                           VERBATIM)
        list(APPEND FXC_FILES "${FILE_WE}-${ENTRY}.fxc")
    endmacro()
    
    fn_add_shader("vs_main" "vs_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("ps_yuv" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("ps_y" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("ps_uv" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("ps_copy" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    
    add_custom_target(fx ALL DEPENDS ${FXC_FILES})

    add_dependencies(server fx)
endif()