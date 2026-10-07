# drstein_core: Qt-free application logic over libstein. Synchronous, testable.
add_library(drstein_core STATIC
  src/format.cpp
  src/progress.cpp
  src/source.cpp
  src/opened.cpp
  src/topology.cpp
  src/structs.cpp
  src/browse.cpp
  src/imaging.cpp
  src/partition_edit.cpp
  src/media.cpp
  src/profiles.cpp
  src/mounts.cpp
)
add_library(drstein::core ALIAS drstein_core)
target_include_directories(drstein_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(drstein_core
  PUBLIC
    stein::core stein::block stein::layout stein::pt stein::fs stein::container stein::volume
    stein::probe stein::platform stein::image stein::ops stein::app stein::mount
  PRIVATE drstein_warnings)
target_compile_features(drstein_core PUBLIC cxx_std_23)
set_target_properties(drstein_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(DRSTEIN_BUILD_TESTS)
  add_executable(drstein_core_tests
    tests/test_main.cpp
    tests/test_format.cpp
    tests/test_topology.cpp
    tests/test_structs.cpp
    tests/test_partition_edit.cpp
    tests/test_imaging.cpp
    tests/test_profiles.cpp
    tests/test_browse.cpp
    tests/test_fixture_export.cpp
  )
  target_include_directories(drstein_core_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/libstein/third_party/doctest)
  target_compile_definitions(drstein_core_tests PRIVATE
    DOCTEST_CONFIG_DOUBLE_STRINGIFY
    STEIN_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/libstein/tests/fixtures")
  target_link_libraries(drstein_core_tests PRIVATE drstein::core drstein_warnings)
  add_test(NAME drstein_core_tests COMMAND drstein_core_tests)
endif()
