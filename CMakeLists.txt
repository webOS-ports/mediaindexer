cmake_minimum_required(VERSION 2.8.7)

project(mediaindexer CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC3)
webos_component(0 1 0)

set(CMAKE_AUTOMOC ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

find_package(Qt5Core REQUIRED)
if(NOT Qt5Core_FOUND)
    message(FATAL_ERROR "Qt5Core module is required!")
endif()

find_package(Qt5Sql REQUIRED)
if(NOT Qt5Sql_FOUND)
    message(FATAL_ERROR "Qt5Sql module is required!")
endif()

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
pkg_check_modules(LUNASERVICE2 REQUIRED luna-service2)

include_directories(${GLIB2_INCLUDE_DIRS})
include_directories(${LUNASERVICE2_INCLUDE_DIRS})

set(SOURCES
    src/main.cpp
    src/mediaindexer.cpp
    src/basesource.cpp
    src/filesystemsource.cpp
    src/mediaindexdatabase.cpp)

add_executable(mediaindexer ${SOURCES})
qt5_use_modules(mediaindexer Core Sql)
target_link_libraries(mediaindexer
    ${GLIB2_LDFLAGS} ${LUNASERVICE2_LDFLAGS} rt pthread)

webos_build_program()
