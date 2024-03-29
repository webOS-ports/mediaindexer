cmake_minimum_required(VERSION 2.8.7)

project(mediaindexer CXX C)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC3)
webos_component(0 1 0)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

pkg_check_modules(GIO2 REQUIRED gio-2.0)
include_directories(${GIO2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GIO2_CFLAGS})

pkg_check_modules(GIO-UNIX REQUIRED gio-unix-2.0)
include_directories(${GIO-UNIX_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GIO-UNIX_CFLAGS_OTHER})

pkg_check_modules(LUNASERVICE2 REQUIRED luna-service2)
include_directories(${LUNASERVICE2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE2_CFLAGS})

pkg_check_modules(PBNJSON_CPP REQUIRED pbnjson_cpp)
include_directories(${PBNJSON_CPP_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PBNJSON_CPP_CFLAGS_OTHER})

pkg_check_modules(SQLITE3 REQUIRED sqlite3)
include_directories(${SQLITE3_INCUDE_DIRS})
webos_add_compiler_flags(ALL ${SQLITE3_CFLAGS})

pkg_check_modules(MOJODB REQUIRED mojodb)
include_directories(${MOJODB_INCUDE_DIRS})
webos_add_compiler_flags(ALL ${MOJODB_CFLAGS})

pkg_check_modules(TAGLIB REQUIRED taglib)
include_directories(${TAGLIB_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${TAGLIB_CFLAGS})

pkg_check_modules(LUNA_SYSMGR_COMMON LunaSysMgrCommon REQUIRED)
include_directories(${LUNA_SYSMGR_COMMON_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNA_SYSMGR_COMMON_CFLAGS})

find_package(Qt6 COMPONENTS Gui )
if (NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Gui)
endif()


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z")
webos_add_compiler_flags(ALL "-DMOJ_LINUX")

file(GLOB SOURCES
    src/Album.cc
    src/MediaFile.cc
    src/MediaStore.cc
    src/MetadataExtractor.cc
    src/Scanner.cc
    src/MediaScanner.cc
    src/MediaScannerServiceApp.cc
    src/MojoMediaDatabase.cc
    src/MojoMediaObjectSerializer.cc
    src/SubtreeWatcher.cc
    src/util.cc
    src/utils.cc
    src/mozilla/fts3_porter.c
    src/mozilla/Normalize.c)

add_executable(mediaindexer ${SOURCES})
target_link_libraries(mediaindexer
    ${GLIB2_LDFLAGS} ${LUNASERVICE2_LDFLAGS} ${PBNJSON_CPP_LDFLAGS}
    ${GIO2_LDFLAGS} ${GIO-UNIX_LDFLAGS} ${GOBJECT2_LDFLAGS} ${SQLITE3_LDFLAGS}
    ${MOJODB_LDFLAGS} ${TAGLIB_LDFLAGS} ${LUNA_SYSMGR_COMMON_LDFLAGS} Qt::Gui rt pthread)

webos_build_daemon()
webos_build_system_bus_files()
