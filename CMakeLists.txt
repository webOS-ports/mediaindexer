cmake_minimum_required(VERSION 2.8.7)

project(mediaindexer CXX)

set(CMAKE_AUTOMOC ON)

find_package(Qt5Core REQUIRED)
if(NOT Qt5Core_FOUND)
    message(FATAL_ERROR "Qt5Core module is required!")
endif()

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
pkg_check_modules(LUNASERVICE2 REQUIRED luna-service2)

include_directories(${GLIB2_INCLUDE_DIRS})
include_directories(${LUNASERVICE2_INCLUDE_DIRS})

set(SOURCES
    src/main.cpp
    src/mediaindexer.cpp
    src/mediatypehandler.cpp)

add_executable(mediaindexer ${SOURCES})
qt5_use_modules(mediaindexer Core)
target_link_libraries(mediaindexer
    ${GLIB2_LDFLAGS} ${LUNASERVICE2_LDFLAGS} rt pthread)
