if(DAYLIGHT_D3D_DEBUG)
    add_definitions("-DDAYLIGHT_D3D_DEBUG")
endif()

set(COMMON_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/log.h
)

set(COMMON_WINDOWS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/platform/windows/winheaders.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/platform/windows/ComWrapper.h
)

set(CLIENT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/client/StreamViewer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/StreamViewer.cpp
)

set(CLIENT_WINDOWS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/client/platform/windows/main.cpp
)

set(SERVER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/server/CaptureData.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/CapturePipeline.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/CapturePipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/StreamServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/StreamServer.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/software/EncoderSoftware.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/software/EncoderSoftware.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/software/ScaleSoftware.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/software/ScaleSoftware.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/software/TextureSoftware.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/software/TextureSoftware.cpp
)

set(SERVER_WINDOWS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/CaptureD3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/CaptureD3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/CapturePipelineD3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/CapturePipelineD3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/CapturePipelineD3DSoft.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/CapturePipelineD3DSoft.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/DeviceManagerD3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/DeviceManagerD3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/EncoderD3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/EncoderD3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/ScaleD3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/platform/windows/ScaleD3D.cpp
)

add_library(common STATIC ${COMMON_SRC} ${COMMON_WINDOWS_SRC})
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common PUBLIC spdlog::spdlog ${FFMPEG_LIBS})

add_executable(client WIN32 ${CLIENT_SRC} ${CLIENT_WINDOWS_SRC})
target_link_libraries(client PUBLIC
	common mbedtls
    Qt6::Widgets Qt6::OpenGLWidgets
	"dxgi.lib" "d3d11.lib"
)
target_compile_definitions(client PUBLIC "-DUNICODE" "-D_UNICODE")

add_executable(server WIN32 ${SERVER_SRC} ${SERVER_WINDOWS_SRC})
target_link_libraries(server PUBLIC
	common mbedtls
	"dxgi.lib" "d3d11.lib"
	"mfuuid.lib" "mfplat.lib"
	"ws2_32.lib"
)
target_compile_definitions(server PUBLIC "-DUNICODE" "-D_UNICODE")


# Separate binaries for better packaging 
set_target_properties(server
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/server"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/server"
)
set_target_properties(client
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/client"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/client"
)


# Compile protobuf definitions
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pb)
set(PB_GENERATED_FILES)
macro(FN_GEN_PB FILE)
    add_custom_command(
        OUTPUT "pb/${FILE}.pb.h" "pb/${FILE}.pb.cc"
        COMMAND protoc "-I=${CMAKE_CURRENT_SOURCE_DIR}/common" "--cpp_out=${CMAKE_CURRENT_BINARY_DIR}/pb/" "${CMAKE_CURRENT_SOURCE_DIR}/common/${FILE}.proto"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/common/${FILE}.proto"
        DEPENDS protoc
        COMMENT "Compiling protobuf: ${FILE}.proto"
        VERBATIM
    )
    list(APPEND PB_GENERATED_FILES "${CMAKE_CURRENT_BINARY_DIR}/pb/${FILE}.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/pb/${FILE}.pb.cc")
endmacro()

fn_gen_pb(packet)
fn_gen_pb(stream)
add_library(protobuf_gen STATIC ${PB_GENERATED_FILES})
target_include_directories(protobuf_gen SYSTEM PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/pb")
target_link_libraries(protobuf_gen PUBLIC libprotobuf-lite)
target_link_libraries(common PUBLIC protobuf_gen)

add_custom_command(TARGET server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:libprotobuf-lite>" "${CMAKE_BINARY_DIR}/bin/server"
	VERBATIM
)
add_custom_command(TARGET client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:libprotobuf-lite>" "${CMAKE_BINARY_DIR}/bin/client"
	VERBATIM
)


# Compile HLSL shader
if(WIN32)
    set(FXC_FILES)

    find_program(FXC fxc DOC "fx compiler")
    if(NOT FXC)
        message(FATAL_ERROR "Cannot find fxc.")
    endif(NOT FXC)

    macro(FN_ADD_SHADER OUTDIR ENTRY PROFILE FILE)
        get_filename_component(HLSL_PATH ${FILE} ABSOLUTE)
        get_filename_component(FILE_WE ${FILE} NAME_WE)
        set(FXC_OUTPUT "${CMAKE_BINARY_DIR}/bin/${OUTDIR}/${FILE_WE}-${ENTRY}.fxc")
        add_custom_command(OUTPUT "${FXC_OUTPUT}"
                           COMMAND ${FXC} /nologo /E "${ENTRY}" /T "${PROFILE}" /Fo "${FXC_OUTPUT}" "${HLSL_PATH}"
                           MAIN_DEPENDENCY "${HLSL_PATH}"
                           COMMENT "Compiling HLSL shader: ${FILE} for ${ENTRY}"
                           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                           VERBATIM)
        list(APPEND FXC_FILES "${FXC_OUTPUT}")
    endmacro()
    
    fn_add_shader("server" "vs_main" "vs_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_yuv" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_y" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_uv" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_copy" "ps_5_0" "./server/platform/windows/rgb2yuv.hlsl")
    
    add_custom_target(fxc DEPENDS ${FXC_FILES})

    add_dependencies(client fxc)
    add_dependencies(server fxc)
endif()


# Copy Qt libraries
if(WIN32)
    find_program(WINDEPLOYQT windeployqt DOC "Qt windows deployment tool")

    macro(FN_DEPLOY_QT EXE)
        add_custom_command(TARGET ${EXE} POST_BUILD
                       COMMAND ${WINDEPLOYQT} --verbose 0 ${EXE}.exe
                       WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${EXE})
    endmacro()

    fn_deploy_qt(client)
endif()