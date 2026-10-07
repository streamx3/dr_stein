cmake_minimum_required(VERSION 3.25)
project(dr_stein VERSION 0.1.0 LANGUAGES CXX)

# Dr Stein: the Qt 6 / QML desktop front-end of libstein. The libraries are a git
# submodule built with add_subdirectory; nothing in them knows about Qt
# (libstein decision D3). See doc/design/01-application-architecture.md.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(DRSTEIN_BUILD_TESTS "Build the core unit tests" ON)
option(DRSTEIN_BUILD_APP   "Build the Qt application (needs Qt 6.5+)" ON)
option(DRSTEIN_WERROR      "Treat warnings as errors" OFF)

# ---------------------------------------------------------------------------
# libstein (submodule). Its own tests and CLI are off here; build them from the
# submodule directory when working on the library.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libstein/CMakeLists.txt")
  message(FATAL_ERROR "libstein submodule is missing: run `git submodule update --init`")
endif()
set(STEIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STEIN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(libstein EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Warnings shared by the application targets (not forced on libstein).
# ---------------------------------------------------------------------------
add_library(drstein_warnings INTERFACE)
if(MSVC)
  target_compile_options(drstein_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
  target_compile_definitions(drstein_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  if(DRSTEIN_WERROR)
    target_compile_options(drstein_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(drstein_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wimplicit-fallthrough)
  if(DRSTEIN_WERROR)
    target_compile_options(drstein_warnings INTERFACE -Werror)
  endif()
endif()
target_compile_definitions(drstein_warnings INTERFACE
  DRSTEIN_VERSION_STRING="${PROJECT_VERSION}")

if(DRSTEIN_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(src/core)
if(DRSTEIN_BUILD_APP)
  add_subdirectory(src/app)   # the Qt facades in src/qt are part of the app's QML module
endif()
