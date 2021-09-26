cmake_minimum_required(VERSION 3.15)
project(daylight CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Begin Options

option(DAYLIGHT_BUILD_GUI "Build GUI targets" ON)
option(DAYLIGHT_D3D_DEBUG "Create dxgi objects in debug mode (Only applied to debug build)" ON)

set(DAYLIGHT_QT6_PATH "" CACHE PATH "Path to Qt6 (example: C:/Qt/6.1.1/msvc2019_64)")

# End options


if(DAYLIGHT_BUILD_GUI)
    if(NOT DAYLIGHT_QT6_PATH STREQUAL "")
        list(APPEND CMAKE_PREFIX_PATH "${DAYLIGHT_QT6_PATH}")
    endif()

    find_package(Qt6 COMPONENTS Widgets OpenGLWidgets REQUIRED)
endif()

add_definitions(-DWINVER=0x0603 -D_WIN32_WINNT=0x0603 /Zc:__cplusplus -DUNICODE -D_UNICODE)

add_subdirectory(external)
add_subdirectory(src)
