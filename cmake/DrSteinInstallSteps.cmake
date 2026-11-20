# Install-time steps of the Linux install (DrSteinLinuxInstall.cmake), included by the
# generated install script. The steps that install files are macros, not functions: file(INSTALL)
# records each file in CMAKE_INSTALL_MANIFEST_FILES of the scope it runs in, and the install script
# writes install_manifest.txt, which the uninstall target reads, from its top scope.
include_guard(GLOBAL)
# The install script runs with CMake's oldest policies; the functions and macros below record
# these (include() scopes the setting to this file).
cmake_policy(VERSION 3.25)

# <path> normalised, without a trailing slash ("/" stays "/").
function(_drstein_norm out path)
  cmake_path(SET path NORMALIZE "${path}")
  string(REGEX REPLACE "(.)/+$" "\\1" path "${path}")
  set(${out} "${path}" PARENT_SCOPE)
endfunction()

# DESTDIR as the install uses it, without a trailing slash.
function(_drstein_destdir out)
  string(REGEX REPLACE "/+$" "" destdir "$ENV{DESTDIR}")
  set(${out} "${destdir}" PARENT_SCOPE)
endfunction()

# The directories the install may create, as paths without DESTDIR: the prefix with every
# directory under it, and each directory outside it that it writes into, each with its parents
# up to (not including) "/".
function(_drstein_candidate_dirs out destdir prefix outside)
  set(result "")
  foreach(dir IN LISTS prefix outside)
    _drstein_norm(dir "${dir}")
    while(NOT dir STREQUAL "/" AND NOT dir STREQUAL "")
      list(APPEND result "${dir}")
      cmake_path(GET dir PARENT_PATH parent)
      if(parent STREQUAL dir)
        break()
      endif()
      set(dir "${parent}")
    endwhile()
  endforeach()
  if(IS_DIRECTORY "${destdir}${prefix}")
    file(GLOB_RECURSE entries LIST_DIRECTORIES true "${destdir}${prefix}/*")
    string(LENGTH "${destdir}" skip)
    foreach(entry IN LISTS entries)
      if(IS_DIRECTORY "${entry}" AND NOT IS_SYMLINK "${entry}")
        string(SUBSTRING "${entry}" ${skip} -1 entry)
        list(APPEND result "${entry}")
      endif()
    endforeach()
  endif()
  list(REMOVE_DUPLICATES result)
  set(${out} "${result}" PARENT_SCOPE)
endfunction()

# The first install rule.
#  - Qt's libraries go to <prefix>/<libdir>. Refuse a prefix where that is on the system library
#    path (the default directories and ld.so.conf): every program there would be offered this Qt.
#  - Refuse to write over an installed dr-stein package (its tree, the PATH link, the .desktop
#    file: <package_paths>): files dpkg manages would change under it, possibly to another Qt.
#    Only for a real install (no DESTDIR); a staged one, such as CPack's, touches nothing of it.
#  - Note which of the directories the install may create exist already; the last rule
#    (drstein_install_record_dirs) records the ones it created in <record>, for `uninstall`.
function(drstein_install_begin libdir exe package_paths record outside)
  cmake_path(SET lib NORMALIZE "${CMAKE_INSTALL_PREFIX}/${libdir}")
  string(REGEX REPLACE "(.)/+$" "\\1" lib "${lib}")
  set(system_dirs /lib /lib64 /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64)
  file(GLOB conf_files /etc/ld.so.conf /etc/ld.so.conf.d/*.conf)
  foreach(conf IN LISTS conf_files)
    file(STRINGS "${conf}" lines)
    foreach(line IN LISTS lines)
      string(REGEX REPLACE "#.*" "" line "${line}")
      string(STRIP "${line}" line)
      if(line MATCHES "^/")   # "include ..." lines name the .d files read here anyway
        string(REGEX REPLACE "(.)/+$" "\\1" line "${line}")
        list(APPEND system_dirs "${line}")
      endif()
    endforeach()
  endforeach()
  set(prefix "${CMAKE_INSTALL_PREFIX}")
  if(prefix STREQUAL "")
    set(prefix /)   # the install script strips the trailing slash
  endif()
  if(lib IN_LIST system_dirs)
    message(FATAL_ERROR
      "Not installing into ${prefix}: the app's own Qt would land in ${lib}, "
      "which is on the system library path. Use a prefix of its own, such as the default "
      "/opt/dr-stein (cmake --install <build> --prefix /opt/dr-stein).")
  endif()
  _drstein_norm(prefix "${prefix}")
  _drstein_destdir(destdir)

  if(destdir STREQUAL "")
    find_program(dpkg_query dpkg-query)
    if(dpkg_query)
      execute_process(COMMAND "${dpkg_query}" -S "${prefix}" "${prefix}/bin/${exe}" ${package_paths}
        OUTPUT_VARIABLE owners ERROR_QUIET)
      string(REPLACE "\n" ";" owners "${owners}")
      foreach(line IN LISTS owners)
        if(line MATCHES "^(.+): (/.*)$")   # "<package>[:<arch>][, <package>...]: <path>"
          set(path "${CMAKE_MATCH_2}")
          string(REGEX REPLACE ":[^, ]+" "" packages "${CMAKE_MATCH_1}")
          string(REPLACE ", " ";" packages "${packages}")
          if("dr-stein" IN_LIST packages)
            message(FATAL_ERROR
              "Not installing: the dr-stein package is installed and owns ${path}. Installing "
              "over it would change files dpkg manages. Remove the package first "
              "(sudo apt remove dr-stein), or install somewhere else with --prefix and, for a "
              "build configured with DRSTEIN_DESKTOP_PREFIX=/usr, another DRSTEIN_DESKTOP_PREFIX.")
          endif()
        endif()
      endforeach()
    endif()
  endif()

  _drstein_candidate_dirs(candidates "${destdir}" "${prefix}" "${outside}")
  set(before "")
  foreach(dir IN LISTS candidates)
    if(IS_DIRECTORY "${destdir}${dir}")
      list(APPEND before "${dir}")
    endif()
  endforeach()
  # Directories an earlier install from this build tree to the same place created stay its own.
  set(key "${destdir}|${prefix}")
  if(EXISTS "${record}")
    file(STRINGS "${record}" earlier)
    if(earlier)
      list(POP_FRONT earlier earlier_key)
      if(earlier_key STREQUAL key AND earlier)
        list(REMOVE_ITEM before ${earlier})
      endif()
    endif()
  endif()
  set_property(GLOBAL PROPERTY DRSTEIN_INSTALL_DIRS_BEFORE "${before}")
  set_property(GLOBAL PROPERTY DRSTEIN_INSTALL_KEY "${key}")
endfunction()

# The last install rule: writes <record>, the directories this install created (paths without
# DESTDIR, after a first line naming DESTDIR and the prefix). `uninstall` removes those it leaves
# empty, and no others.
function(drstein_install_record_dirs record outside)
  get_property(before GLOBAL PROPERTY DRSTEIN_INSTALL_DIRS_BEFORE)
  get_property(key GLOBAL PROPERTY DRSTEIN_INSTALL_KEY)
  set(prefix "${CMAKE_INSTALL_PREFIX}")
  if(prefix STREQUAL "")
    set(prefix /)
  endif()
  _drstein_norm(prefix "${prefix}")
  _drstein_destdir(destdir)
  _drstein_candidate_dirs(candidates "${destdir}" "${prefix}" "${outside}")
  set(created "")
  foreach(dir IN LISTS candidates)
    if(IS_DIRECTORY "${destdir}${dir}" AND NOT IS_SYMLINK "${destdir}${dir}" AND NOT dir IN_LIST before)
      list(APPEND created "${dir}")
    endif()
  endforeach()
  list(SORT created)
  string(REPLACE ";" "\n" created "${created}")
  file(WRITE "${record}" "${key}\n${created}\n")
endfunction()

# Points Qt's deploy script, which writes to $ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}, at an empty
# staging folder, with the tree's directory names.
macro(drstein_deploy_qt_begin stage bindir libdir)
  set(_drstein_stage "${stage}")
  set(_drstein_saved_prefix "${CMAKE_INSTALL_PREFIX}")
  set(_drstein_saved_destdir "$ENV{DESTDIR}")
  set(_drstein_saved_manifest "${CMAKE_INSTALL_MANIFEST_FILES}")
  file(REMOVE_RECURSE "${_drstein_stage}")
  set(CMAKE_INSTALL_PREFIX "${_drstein_stage}")
  set(ENV{DESTDIR} "")
  unset(QT_DEPLOY_PREFIX)   # Qt derives it from the two above
  set(QT_DEPLOY_BIN_DIR "${bindir}")
  set(QT_DEPLOY_LIB_DIR "${libdir}")
  set(QT_DEPLOY_LIBEXEC_DIR libexec)
  set(QT_DEPLOY_PLUGINS_DIR plugins)
  set(QT_DEPLOY_QML_DIR qml)
  set(QT_DEPLOY_TRANSLATIONS_DIR translations)
endmacro()

# Qt's deploy copies each library with its symlink chain (libQt6Core.so.6 -> libQt6Core.so.6.12.0).
# Only the soname is ever loaded, and CPack's Installed-Size counts a symlink at the size of its
# target, so a link that is the only one to a file in the same directory takes the file's place.
function(drstein_flatten_library_links dir)
  file(GLOB entries LIST_DIRECTORIES false "${dir}/*")
  set(targets_once "")
  set(targets_more "")
  foreach(entry IN LISTS entries)
    if(IS_SYMLINK "${entry}")
      file(READ_SYMLINK "${entry}" target)
      if(target IN_LIST targets_once)
        list(APPEND targets_more "${target}")
      else()
        list(APPEND targets_once "${target}")
      endif()
    endif()
  endforeach()
  foreach(entry IN LISTS entries)
    if(IS_SYMLINK "${entry}")
      file(READ_SYMLINK "${entry}" target)
      if(NOT target MATCHES "/" AND NOT target IN_LIST targets_more
         AND EXISTS "${dir}/${target}" AND NOT IS_SYMLINK "${dir}/${target}")
        file(RENAME "${dir}/${target}" "${entry}")
      endif()
    endif()
  endforeach()
endfunction()

# Qt's deploy follows QtQuick.Controls' optional imports and brings every Qt Quick Controls style
# (Fusion, Material, Universal, Imagine, FluentWinUI3) with its libraries. The app sets <keep>
# before loading any QML (main.cpp), which wins over QT_QUICK_CONTROLS_STYLE and the -style
# argument, so the others can never load.
function(_drstein_prune_qt_styles stage libdir keep)
  set(controls "${stage}/qml/QtQuick/Controls")
  file(GLOB styles LIST_DIRECTORIES true RELATIVE "${controls}" "${controls}/*")
  foreach(style IN LISTS styles)
    if(IS_DIRECTORY "${controls}/${style}" AND NOT style STREQUAL keep AND NOT style STREQUAL "impl")
      file(REMOVE_RECURSE "${controls}/${style}")
      file(GLOB libs "${stage}/${libdir}/libQt6QuickControls2${style}.so*"
                     "${stage}/${libdir}/libQt6QuickControls2${style}StyleImpl.so*")
      if(libs)
        file(REMOVE ${libs})
      endif()
    endif()
  endforeach()
endfunction()

# Strips the ELF libraries in <dir>. Qt's own come stripped (their debug information is in
# separate files the deploy leaves out); the ICU libraries of the Qt kits do not.
function(_drstein_strip_libraries strip dir)
  file(GLOB libs LIST_DIRECTORIES false "${dir}/*.so*")
  foreach(lib IN LISTS libs)
    if(IS_SYMLINK "${lib}")
      continue()
    endif()
    file(READ "${lib}" magic LIMIT 4 HEX)
    if(magic STREQUAL "7f454c46")
      execute_process(COMMAND "${strip}" --strip-unneeded "${lib}" RESULT_VARIABLE rc)
      if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${strip} could not strip ${lib}")
      endif()
    endif()
  endforeach()
endfunction()

# Writes <notices>: the third-party code inside the deployed Qt, from Qt's documentation
# (tools/qt-third-party-notices.py), or where to find that list when the documentation is missing.
function(_drstein_write_qt_notices python tool docs qt_version stage notices)
  cmake_path(GET notices PARENT_PATH dir)
  file(MAKE_DIRECTORY "${dir}")
  if(docs)
    execute_process(COMMAND "${python}" "${tool}" --docs "${docs}" --tree "${stage}"
        --qt-version "${qt_version}" --output "${notices}"
      RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${tool} could not write ${notices}")
    endif()
  else()
    file(WRITE "${notices}" "Third-party code in Qt ${qt_version}\n\n"
      "The Qt libraries, plugins and QML modules installed with Dr Stein contain third-party code\n"
      "under licences of its own. Qt's documentation lists each piece with its copyright notices\n"
      "and licence text: https://doc.qt.io/qt-6/licenses-used-in-qt.html (the pages of Qt ${qt_version}).\n"
      "This build did not have Qt's documentation at hand to copy them here.\n")
  endif()
endfunction()

# Restores the real destination and installs the staged tree into it. Everything Qt's deploy
# brings is a library, a plugin or data, so files go in 0644 (Debian's mode for shared libraries)
# and directories 0755. Before that: the unused Qt Quick Controls styles go, the libraries are
# stripped when the install strips (--strip, CPack), and the third-party notices of what is left
# go to <docdir>/qt-third-party.txt.gz. The stage is removed afterwards; the next install makes
# it afresh. Expects _drstein_keep_style, _drstein_strip, _drstein_python, _drstein_notices_tool,
# _drstein_qt_docs, _drstein_qt_version, _drstein_notices, _drstein_gzip and _drstein_docdir.
macro(drstein_deploy_qt_end)
  set(CMAKE_INSTALL_PREFIX "${_drstein_saved_prefix}")
  set(ENV{DESTDIR} "${_drstein_saved_destdir}")
  set(CMAKE_INSTALL_MANIFEST_FILES "${_drstein_saved_manifest}")
  unset(QT_DEPLOY_PREFIX)
  if(NOT EXISTS "${_drstein_stage}/${QT_DEPLOY_BIN_DIR}/qt.conf")
    message(FATAL_ERROR "Qt's deployment left no ${QT_DEPLOY_BIN_DIR}/qt.conf in ${_drstein_stage}; "
      "the installed app would not find its plugins and QML modules.")
  endif()
  _drstein_prune_qt_styles("${_drstein_stage}" "${QT_DEPLOY_LIB_DIR}" "${_drstein_keep_style}")
  drstein_flatten_library_links("${_drstein_stage}/${QT_DEPLOY_LIB_DIR}")
  if(CMAKE_INSTALL_DO_STRIP AND _drstein_strip)
    _drstein_strip_libraries("${_drstein_strip}" "${_drstein_stage}/${QT_DEPLOY_LIB_DIR}")
  endif()
  _drstein_write_qt_notices("${_drstein_python}" "${_drstein_notices_tool}" "${_drstein_qt_docs}"
    "${_drstein_qt_version}" "${_drstein_stage}" "${_drstein_notices}")
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" TYPE DIRECTORY FILES "${_drstein_stage}/"
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  if(IS_ABSOLUTE "${_drstein_docdir}")
    set(_drstein_docdest "${_drstein_docdir}")
  else()
    set(_drstein_docdest "${CMAKE_INSTALL_PREFIX}/${_drstein_docdir}")
  endif()
  execute_process(COMMAND "${_drstein_gzip}" -9 -n -c "${_drstein_notices}"
    OUTPUT_FILE "${_drstein_notices}.gz" RESULT_VARIABLE _drstein_rc)
  if(NOT _drstein_rc EQUAL 0)
    message(FATAL_ERROR "gzip could not compress ${_drstein_notices}")
  endif()
  file(INSTALL DESTINATION "${_drstein_docdest}" TYPE FILE FILES "${_drstein_notices}.gz"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
  file(REMOVE_RECURSE "${_drstein_stage}")
endmacro()

# <link_dir>/<name> -> <prefix>/<bindir>/<name>, the app on PATH. The link is made in a scratch
# folder and installed with file(INSTALL), so it honours DESTDIR and lands in the manifest.
macro(drstein_install_launcher_link link_dir bindir name scratch)
  set(_drstein_link_target "${CMAKE_INSTALL_PREFIX}/${bindir}/${name}")
  if(NOT "${link_dir}/${name}" STREQUAL "${_drstein_link_target}")
    file(REMOVE_RECURSE "${scratch}")
    file(MAKE_DIRECTORY "${scratch}")
    file(CREATE_LINK "${_drstein_link_target}" "${scratch}/${name}" SYMBOLIC)
    file(INSTALL DESTINATION "${link_dir}" TYPE FILE FILES "${scratch}/${name}")
  endif()
endmacro()
