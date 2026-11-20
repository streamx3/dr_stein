# The `uninstall` target (Linux, DrSteinLinuxInstall.cmake):
#
#   cmake -D MANIFEST=<build>/install_manifest.txt -D DIRS=<build>/install_manifest_dirs.txt -P DrSteinUninstall.cmake
#
# Removes every file and symlink install_manifest.txt lists, under $ENV{DESTDIR} as the install
# put them. Then, deepest first, the directories DIRS lists, which are the ones the install
# created (DrSteinInstallSteps.cmake), when they are empty; directories that existed before the
# install stay, empty or not. Files that an installed dr-stein package owns stay too (the install
# refuses to write over one, so they are the package's own).
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED MANIFEST OR NOT DEFINED DIRS)
  message(FATAL_ERROR "usage: cmake -D MANIFEST=<install_manifest.txt> -D DIRS=<install_manifest_dirs.txt> -P ${CMAKE_CURRENT_LIST_FILE}")
endif()
if(NOT EXISTS "${MANIFEST}")
  message(FATAL_ERROR "Nothing to uninstall: ${MANIFEST} does not exist. The install writes it.")
endif()

string(REGEX REPLACE "/+$" "" destdir "$ENV{DESTDIR}")
file(STRINGS "${MANIFEST}" files)

set(owned "")
if(destdir STREQUAL "")
  find_program(DPKG_QUERY dpkg-query)
  if(DPKG_QUERY)
    execute_process(COMMAND "${DPKG_QUERY}" -L dr-stein
      OUTPUT_VARIABLE owned RESULT_VARIABLE rc ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(rc EQUAL 0)
      string(REPLACE "\n" ";" owned "${owned}")
    else()
      set(owned "")
    endif()
  endif()
endif()

set(kept 0)
foreach(file IN LISTS files)
  if(file STREQUAL "")
    continue()
  endif()
  set(path "${destdir}${file}")
  if(file IN_LIST owned)
    message(STATUS "Kept (the installed dr-stein package owns it): ${path}")
    math(EXPR kept "${kept} + 1")
    continue()
  endif()
  if(IS_SYMLINK "${path}" OR EXISTS "${path}")
    if(IS_DIRECTORY "${path}" AND NOT IS_SYMLINK "${path}")
      message(STATUS "Kept (now a directory): ${path}")
      continue()
    endif()
    file(REMOVE "${path}")
    if(IS_SYMLINK "${path}" OR EXISTS "${path}")
      message(FATAL_ERROR "Could not remove ${path} (run the uninstall with the rights the install had, e.g. sudo).")
    endif()
    message(STATUS "Removed: ${path}")
  else()
    message(STATUS "Already gone: ${path}")
  endif()
endforeach()

# The directories the install created. Children sort after their parents, so a descending sort
# empties the deepest first; rmdir removes a directory only when it is empty.
if(EXISTS "${DIRS}")
  file(STRINGS "${DIRS}" dirs)
  list(POP_FRONT dirs)   # DESTDIR|prefix of that install
  list(REMOVE_ITEM dirs "" "/")
  list(SORT dirs ORDER DESCENDING)
  foreach(dir IN LISTS dirs)
    set(path "${destdir}${dir}")
    if(IS_DIRECTORY "${path}" AND NOT IS_SYMLINK "${path}")
      execute_process(COMMAND rmdir -- "${path}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
      if(rc EQUAL 0)
        message(STATUS "Removed directory: ${path}")
      endif()
    endif()
  endforeach()
  file(REMOVE "${DIRS}")
else()
  message(STATUS "No record of the directories the install created (${DIRS}); directories are left in place.")
endif()
if(kept GREATER 0)
  message(STATUS "${kept} files belong to the installed dr-stein package; remove it with: sudo apt remove dr-stein")
endif()
