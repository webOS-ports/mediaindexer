# Unit tests for the parts of mediaindexer that need no bus or db8.
# Off by default so image builds are unaffected:
#   cmake -DBUILD_TESTING=ON ...

add_executable(mediaindexer-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/PermissionRulesTest.cc
    ${PROJECT_SOURCE_DIR}/src/PermissionRules.cc)
target_include_directories(mediaindexer-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME mediaindexer-unit COMMAND mediaindexer-tests)

add_executable(mediaindexer-owned-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/OwnedEntriesTest.cc
    ${PROJECT_SOURCE_DIR}/src/OwnedEntries.cc)
target_include_directories(mediaindexer-owned-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME mediaindexer-owned COMMAND mediaindexer-owned-tests)

add_executable(mediaindexer-videosize-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoSizeTest.cc
    ${PROJECT_SOURCE_DIR}/src/VideoSize.cc)
target_include_directories(mediaindexer-videosize-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME mediaindexer-videosize COMMAND mediaindexer-videosize-tests)

add_executable(mediaindexer-folderalbum-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/FolderAlbumTest.cc
    ${PROJECT_SOURCE_DIR}/src/FolderAlbum.cc)
target_include_directories(mediaindexer-folderalbum-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME mediaindexer-folderalbum COMMAND mediaindexer-folderalbum-tests)

add_executable(mediaindexer-extractoralbum-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/MetadataExtractorAlbumTest.cc
    ${PROJECT_SOURCE_DIR}/src/MetadataExtractor.cc
    ${PROJECT_SOURCE_DIR}/src/MediaFile.cc
    ${PROJECT_SOURCE_DIR}/src/FolderAlbum.cc
    ${PROJECT_SOURCE_DIR}/src/VideoSize.cc)
target_include_directories(mediaindexer-extractoralbum-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mediaindexer-extractoralbum-tests
    ${GLIB2_LDFLAGS} ${GIO2_LDFLAGS} ${TAGLIB_LDFLAGS} Qt::Gui)

add_test(NAME mediaindexer-extractoralbum COMMAND mediaindexer-extractoralbum-tests)
