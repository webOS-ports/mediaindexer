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
