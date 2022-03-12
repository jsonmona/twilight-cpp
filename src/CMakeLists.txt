if(TWILIGHT_D3D_DEBUG)
    add_definitions("-DTWILIGHT_D3D_DEBUG")
endif()

if(TWILIGHT_WRITE_SSLKEYLOG)
    add_definitions("-DTWILIGHT_WRITE_SSLKEYLOG")
endif()

set(COMMON_SRC
    ./common/ByteBuffer.h
    ./common/DesktopFrame.h
    ./common/ffmpeg-headers.h
    ./common/RingBuffer.h
    
    ./common/CertHash.h
    ./common/CertHash.cpp
    ./common/CertStore.h
    ./common/CertStore.cpp
    ./common/Keypair.h
    ./common/Keypair.cpp
    ./common/log.h
    ./common/log.cpp
    ./common/Rational.h
    ./common/Rational.cpp
    ./common/StatisticMixer.h
    ./common/StatisticMixer.cpp
    ./common/util.h
    ./common/util.cpp

    ./common/net/NetworkServer.h
    ./common/net/NetworkServer.cpp
    ./common/net/NetworkSocket.h
    ./common/net/NetworkSocket.cpp

    ./common/platform/software/OpenH264Loader.h
    ./common/platform/software/OpenH264Loader.cpp
    ./common/platform/software/ScaleSoftware.h
    ./common/platform/software/ScaleSoftware.cpp
    ./common/platform/software/TextureSoftware.h
    ./common/platform/software/TextureSoftware.cpp
)

set(COMMON_WINDOWS_SRC
    ./common/platform/windows/winheaders.h
    ./common/platform/windows/ComWrapper.h

    ./common/platform/windows/DxgiHelper.h
    ./common/platform/windows/DxgiHelper.cpp
    ./common/platform/windows/OpenH264LoaderWin32.h
    ./common/platform/windows/OpenH264LoaderWin32.cpp
    ./common/platform/windows/QPCTimer.h
    ./common/platform/windows/QPCTimer.cpp
)

set(CLIENT_SRC
    ./client/StreamViewerBase.h

    ./client/FlowLayout.h
    ./client/FlowLayout.cpp
    ./client/HostList.h
    ./client/HostList.cpp
    ./client/HubWindow.h
    ./client/HubWindow.cpp
    ./client/HubWindow.ui
    ./client/HubWindowAddHostDialog.ui
    ./client/HubWindowHostItem.h
    ./client/HubWindowHostItem.cpp
    ./client/HubWindowHostItem.ui
    ./client/StreamClient.h
    ./client/StreamClient.cpp
    ./client/StreamWindow.h
    ./client/StreamWindow.cpp

    ./client/platform/software/DecoderSoftware.h
    ./client/platform/software/DecoderSoftware.cpp
)

set(CLIENT_WINDOWS_SRC
    ./client/platform/windows/StreamViewerD3D.h
    ./client/platform/windows/StreamViewerD3D.cpp

    ./client/platform/windows/main.cpp
)

set(SERVER_SRC
    ./server/CapturePipeline.h

    ./server/AudioEncoder.h
    ./server/AudioEncoder.cpp
    ./server/CapturePipelineFactory.h
    ./server/CapturePipelineFactory.cpp
    ./server/Connection.h
    ./server/Connection.cpp
    ./server/KnownClients.h
    ./server/KnownClients.cpp
    ./server/StreamServer.h
    ./server/StreamServer.cpp

    ./server/platform/software/EncoderSoftware.h
    ./server/platform/software/EncoderSoftware.cpp
)

set(SERVER_WINDOWS_SRC
    ./server/platform/windows/main.cpp

    ./server/platform/windows/CaptureWin32.h
    
    ./server/platform/windows/AudioCaptureWASAPI.h
    ./server/platform/windows/AudioCaptureWASAPI.cpp
    ./server/platform/windows/CaptureD3D.h
    ./server/platform/windows/CaptureD3D.cpp
    ./server/platform/windows/CapturePipelineD3DMF.h
    ./server/platform/windows/CapturePipelineD3DMF.cpp
    ./server/platform/windows/CapturePipelineD3DSoft.h
    ./server/platform/windows/CapturePipelineD3DSoft.cpp
    ./server/platform/windows/CapturePipelineFactroryWin32.h
    ./server/platform/windows/CapturePipelineFactroryWin32.cpp
    ./server/platform/windows/EncoderMF.h
    ./server/platform/windows/EncoderMF.cpp
    ./server/platform/windows/ScaleD3D.h
    ./server/platform/windows/ScaleD3D.cpp
)

find_package(Git)
if(Git_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh --always --abbrev=40 --dirty
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE TWILIGHT_GIT_COMMIT
            ENCODING AUTO
            TIMEOUT 10
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND
            "${GIT_EXECUTABLE}" log -1 --format=%ad --date=unix
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE TWILIGHT_GIT_DATE
            ENCODING AUTO
            TIMEOUT 10
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    configure_file(./common/version.cpp.in ./version.cpp @ONLY)
    add_library(git-info STATIC ./common/version.h "${CMAKE_CURRENT_BINARY_DIR}/version.cpp")
    target_include_directories(git-info PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(git-info PRIVATE TWILIGHT_HAS_GIT_INFO)
else()
    message(WARNING "Git executable not found. Version info will be missing.")
    configure_file(./common/version.cpp.in ./version.cpp COPYONLY)
    add_library(git-info STATIC ./common/version.h "${CMAKE_CURRENT_BINARY_DIR}/version.cpp")
    target_include_directories(git-info PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_library(common STATIC ${COMMON_SRC} ${COMMON_WINDOWS_SRC})
target_include_directories(common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(common PUBLIC
    git-info
    mbedtls openh264 opus spdlog::spdlog toml11
    ${FFMPEG_LIBS}
    "winmm.lib"
)

add_executable(server ${SERVER_SRC} ${SERVER_WINDOWS_SRC})
target_link_libraries(server PUBLIC
    common
    "dxgi.lib" "d3d11.lib"
    "mfuuid.lib" "mfplat.lib"
)
set_target_properties(server
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/server"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/server"
)

if(TWILIGHT_BUILD_GUI)
    add_executable(client WIN32 ${CLIENT_SRC} ${CLIENT_WINDOWS_SRC})
    target_link_libraries(client PUBLIC
        common imgui cubeb
        Qt6::Widgets Qt6::OpenGLWidgets
        "dxgi.lib" "d3d11.lib"
    )
    set_target_properties(client
        PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/client"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/client"
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON
    )
endif()


# Compile protobuf definitions
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pb")
set(PB_GENERATED_FILES)
macro(FN_GEN_PB FILE)
    add_custom_command(
        OUTPUT "pb/${FILE}.pb.h" "pb/${FILE}.pb.cc"
        COMMAND protoc "-I=${CMAKE_CURRENT_SOURCE_DIR}/common/protobuf" "--cpp_out=${CMAKE_CURRENT_BINARY_DIR}/pb/" "${CMAKE_CURRENT_SOURCE_DIR}/common/protobuf/${FILE}.proto"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/common/protobuf/${FILE}.proto"
        DEPENDS protoc
        COMMENT "Compiling protobuf: ${FILE}.proto"
        VERBATIM
    )
    list(APPEND PB_GENERATED_FILES "${CMAKE_CURRENT_BINARY_DIR}/pb/${FILE}.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/pb/${FILE}.pb.cc")
endmacro()

fn_gen_pb(auth)
fn_gen_pb(control)
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
if(TWILIGHT_BUILD_GUI)
    add_custom_command(TARGET client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:libprotobuf-lite>" "${CMAKE_BINARY_DIR}/bin/client"
        VERBATIM
    )
endif()


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
                           COMMAND ${FXC} /nologo /E "${ENTRY}" /T "${PROFILE}_4_0" /Fo "${FXC_OUTPUT}" "${HLSL_PATH}"
                           MAIN_DEPENDENCY "${HLSL_PATH}"
                           COMMENT "Compiling HLSL shader: ${FILE} for ${ENTRY}"
                           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                           VERBATIM)
        list(APPEND FXC_FILES "${FXC_OUTPUT}")
    endmacro()
    
    fn_add_shader("server" "vs_main" "vs" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_yuv" "ps" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_y" "ps" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_uv" "ps" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("server" "ps_copy" "ps" "./server/platform/windows/rgb2yuv.hlsl")
    fn_add_shader("client" "vs_full" "vs" "./client/platform/windows/viewer.hlsl")
    fn_add_shader("client" "vs_box" "vs" "./client/platform/windows/viewer.hlsl")
    fn_add_shader("client" "ps_main" "ps" "./client/platform/windows/viewer.hlsl")
    
    add_custom_target(fxc DEPENDS ${FXC_FILES})

    add_dependencies(server fxc)

    if(TWILIGHT_BUILD_GUI)
        add_dependencies(client fxc)
    endif()
endif()


# Copy Qt libraries
if(WIN32 AND TWILIGHT_BUILD_GUI)
    find_program(WINDEPLOYQT windeployqt DOC "Qt windows deployment tool")

    if(WINDEPLOYQT)
        macro(FN_DEPLOY_QT EXE)
            add_custom_command(TARGET ${EXE} POST_BUILD
                           COMMAND "${WINDEPLOYQT}" --verbose 0 ${EXE}.exe
                           WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${EXE}")
        endmacro()

        fn_deploy_qt(client)
    endif()
endif()
