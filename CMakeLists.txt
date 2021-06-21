cmake_minimum_required(VERSION 3.1)
project(daylight CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Begin Options

option(BUILD_GUI "Build GUI targets" ON)

set(QT6_PATH "" CACHE PATH "Path to Qt6 (example: C:/Qt/6.1.1/msvc2019_64)")

# End options


if(NOT QT6_PATH STREQUAL "")
    list(APPEND CMAKE_PREFIX_PATH "${QT6_PATH}")
endif()

if(BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
endif()

if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

add_subdirectory(external)
add_subdirectory(src)
