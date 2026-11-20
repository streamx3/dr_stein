# The Linux install: one self-contained tree that carries its own Qt, plus the few files the
# desktop needs outside it. Included by the top-level CMakeLists.txt on Linux before libstein and
# the app, so before anything includes GNUInstallDirs. `cmake --install`, `make install` and the
# .deb (DrSteinPackaging.cmake) all produce this layout:
#
#   <prefix>/                      CMAKE_INSTALL_PREFIX, /opt/dr-stein unless set
#     bin/dr-stein                 the app; RUNPATH $ORIGIN/../lib
#     bin/qt.conf                  Prefix = .. (from Qt's deploy). Started through the PATH link,
#                                  Qt looks next to the link instead and finds the same plugins/
#                                  and qml/ from where libQt6Core.so.6 is (Qt's relocatable prefix)
#     lib/                         Qt's libraries, ICU and libstein's (RUNPATH $ORIGIN)
#     plugins/  qml/               Qt's plugins and the QML modules the app imports
#   <desktop>/bin/dr-stein         symlink to <prefix>/bin/dr-stein, so the app is on PATH
#   <desktop>/share/applications/dr-stein.desktop
#   <desktop>/share/icons/hicolor/{scalable,symbolic}/apps/dr-stein*.svg
#   <desktop>/share/man/man1/dr-stein.1.gz
#   <desktop>/share/doc/dr-stein/  copyright (Debian's format: the licences of everything above)
#                                  and qt-third-party.txt.gz (the third-party code inside Qt)
#
# <desktop> is DRSTEIN_DESKTOP_PREFIX: /usr/local for a plain install (local software, outside
# what the package manager owns; both XDG_DATA_DIRS and PATH include it by default) and /usr in
# the .deb (the "package" preset sets it). Empty puts those files inside the prefix, without the
# PATH link.
#
# Qt's libraries never go to a directory on the system library path: the install refuses a
# prefix whose lib/ is /lib, /usr/lib, /usr/local/lib or listed in ld.so.conf. Nor does it write
# over an installed dr-stein package; `sudo apt remove dr-stein` first, or another --prefix.
#
# Qt's own deployment (qt_generate_deploy_qml_app_script: qmlimportscanner, then the libraries,
# plugins and QML modules the binary needs) fills lib/, plugins/ and qml/ and writes qt.conf. It
# runs into a staging folder in the build tree that is then installed with file(INSTALL) from the
# top scope of the install script: files Qt's deploy functions install themselves are missing
# from install_manifest.txt, and the `uninstall` target removes what that file lists.
#
# Only the xcb platform plugin is deployed (Qt's deploy has no other for Linux desktops): on a
# Wayland session the app runs through XWayland, and main() makes xcb the fallback of any
# QT_QPA_PLATFORM. Without an X server (no XWayland) it does not start.
include_guard(GLOBAL)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/opt/dr-stein" CACHE PATH
    "Install prefix: the app's own tree (bin, lib, plugins, qml) with its Qt" FORCE)
endif()

set(DRSTEIN_DESKTOP_PREFIX "/usr/local" CACHE PATH
  "Where the PATH link bin/dr-stein, the .desktop file, the icons, the manual page and the licences go (/usr in the .deb); empty keeps them inside the install prefix")
set(DRSTEIN_DESKTOP_PREFIX_PATH "")   # normalised, no trailing slash; empty when inside the prefix
if(DRSTEIN_DESKTOP_PREFIX)
  if(NOT IS_ABSOLUTE "${DRSTEIN_DESKTOP_PREFIX}")
    message(FATAL_ERROR "DRSTEIN_DESKTOP_PREFIX must be an absolute path, not \"${DRSTEIN_DESKTOP_PREFIX}\"")
  endif()
  cmake_path(NORMAL_PATH DRSTEIN_DESKTOP_PREFIX OUTPUT_VARIABLE DRSTEIN_DESKTOP_PREFIX_PATH)
  string(REGEX REPLACE "(.)/+$" "\\1" DRSTEIN_DESKTOP_PREFIX_PATH "${DRSTEIN_DESKTOP_PREFIX_PATH}")
  # Absolute destinations: DESTDIR still applies, and CPack stages them like the rest.
  set(DRSTEIN_DESKTOP_DATADIR "${DRSTEIN_DESKTOP_PREFIX_PATH}/share")
  set(DRSTEIN_DESKTOP_BINDIR "${DRSTEIN_DESKTOP_PREFIX_PATH}/bin")
else()
  include(GNUInstallDirs)
  set(DRSTEIN_DESKTOP_DATADIR "${CMAKE_INSTALL_DATADIR}")
  set(DRSTEIN_DESKTOP_BINDIR "")
endif()
set(DRSTEIN_DESKTOP_DOCDIR "${DRSTEIN_DESKTOP_DATADIR}/doc/dr-stein")
set(DRSTEIN_DESKTOP_MANDIR "${DRSTEIN_DESKTOP_DATADIR}/man/man1")

# Directories the install creates are 0755 whatever the umask (Mint's is 002).
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# The private library directory of the tree, the same name everywhere (not GNUInstallDirs'
# lib64 or lib/<multiarch>): bin/'s RUNPATH, Qt's deploy and the package's shlibdeps use it.
set(DRSTEIN_INSTALL_LIBDIR lib)

# The one Qt Quick Controls style the app uses (main.cpp sets it); the others are not deployed.
set(DRSTEIN_QT_STYLE Basic)

# Writes the third-party notices of the deployed Qt (tools/qt-third-party-notices.py) at install
# time; libstein needs the same Python to build.
find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)
# The manual page, the Qt notices and the package's changelog are installed gzip-compressed.
find_program(DRSTEIN_GZIP gzip REQUIRED)

set(DRSTEIN_QT_DOCS_DIR "" CACHE PATH
  "Qt's documentation for the Qt this builds with (<Qt>/Docs/Qt-<version>), for the third-party notices of the installed Qt; empty: next to the Qt kit")

set(_drstein_install_steps "${CMAKE_CURRENT_LIST_DIR}/DrSteinInstallSteps.cmake")
set(_drstein_dirs_record "${CMAKE_BINARY_DIR}/install_manifest_dirs.txt")

# A value as a quoted CMake argument, for code written into the install script.
function(_drstein_quote out value)
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(REPLACE "$" "\\$" value "${value}")
  set(${out} "\"${value}\"" PARENT_SCOPE)
endfunction()

# The directories outside the prefix that the install writes into, for the record of the
# directories it creates (DrSteinInstallSteps.cmake). Inside the prefix it looks for itself.
set(_drstein_outside_dirs "")
if(DRSTEIN_DESKTOP_BINDIR)
  list(APPEND _drstein_outside_dirs "${DRSTEIN_DESKTOP_BINDIR}" "${DRSTEIN_DESKTOP_DATADIR}/applications"
    "${DRSTEIN_DESKTOP_DATADIR}/icons/hicolor/scalable/apps" "${DRSTEIN_DESKTOP_DATADIR}/icons/hicolor/symbolic/apps"
    "${DRSTEIN_DESKTOP_DOCDIR}" "${DRSTEIN_DESKTOP_MANDIR}")
endif()

# The first install rule: refuse a prefix that would put Qt on the system library path, or that
# holds an installed dr-stein package, and note which directories exist before anything is written.
set(_drstein_package_paths "")
if(DRSTEIN_DESKTOP_BINDIR)
  list(APPEND _drstein_package_paths "${DRSTEIN_DESKTOP_BINDIR}/dr-stein" "${DRSTEIN_DESKTOP_DATADIR}/applications/dr-stein.desktop")
endif()
_drstein_quote(_q_steps "${_drstein_install_steps}")
_drstein_quote(_q_libdir "${DRSTEIN_INSTALL_LIBDIR}")
_drstein_quote(_q_record "${_drstein_dirs_record}")
_drstein_quote(_q_outside "${_drstein_outside_dirs}")
_drstein_quote(_q_package_paths "${_drstein_package_paths}")
install(CODE "include(${_q_steps})
drstein_install_begin(${_q_libdir} \"$<TARGET_FILE_NAME:dr_stein>\" ${_q_package_paths} ${_q_record} ${_q_outside})")

# The last install rule (the top-level CMakeLists.txt calls this after everything else): records
# the directories the install created, the ones `uninstall` may remove again.
function(drstein_install_finish)
  install(CODE "include(${_q_steps})
drstein_install_record_dirs(${_q_record} ${_q_outside})")
endfunction()

# `make uninstall` / `ninja uninstall`: removes what install_manifest.txt lists, under DESTDIR
# like the install, then those of the directories the install created that this leaves empty.
add_custom_target(uninstall
  COMMAND "${CMAKE_COMMAND}"
    -D "MANIFEST=${CMAKE_BINARY_DIR}/install_manifest.txt"
    -D "DIRS=${_drstein_dirs_record}"
    -P "${CMAKE_CURRENT_LIST_DIR}/DrSteinUninstall.cmake"
  COMMENT "Removing the files listed in install_manifest.txt"
  USES_TERMINAL
  VERBATIM)

# Every shared library this project builds that <target> loads at run time (libstein's modules
# today), following link dependencies through aliases, static and interface libraries.
function(_drstein_project_shared_libraries out target)
  set(result "")
  set(seen "")
  set(queue "${target}")
  while(NOT queue STREQUAL "")   # not while(queue): a list ending in "...-NOTFOUND" counts as false
    list(POP_FRONT queue item)
    if(NOT TARGET "${item}")
      continue()   # a plain library name or flag, or an imported target local to another directory
    endif()
    get_target_property(aliased "${item}" ALIASED_TARGET)
    if(aliased)
      set(item "${aliased}")
    endif()
    if(item IN_LIST seen)
      continue()
    endif()
    list(APPEND seen "${item}")
    get_target_property(imported "${item}" IMPORTED)
    if(imported)
      continue()   # Qt, fuse3: Qt's deploy and the package dependencies cover those
    endif()
    get_target_property(type "${item}" TYPE)
    if(type STREQUAL "SHARED_LIBRARY")
      list(APPEND result "${item}")
    endif()
    set(deps "")
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
      get_target_property(deps "${item}" LINK_LIBRARIES)
    endif()
    get_target_property(interface_deps "${item}" INTERFACE_LINK_LIBRARIES)
    foreach(dep IN LISTS deps interface_deps)
      if(dep MATCHES "-NOTFOUND$")
        continue()   # the property is not set
      endif()
      if(dep MATCHES "^\\$<LINK_ONLY:(.+)>$")
        set(dep "${CMAKE_MATCH_1}")
      endif()
      list(APPEND queue "${dep}")
    endforeach()
  endwhile()
  set(${out} "${result}" PARENT_SCOPE)
endfunction()

# A licence file as the text of a paragraph in Debian's copyright format: every line indented by
# one space, empty lines as " .".
function(_drstein_dep5_text out file)
  file(READ "${file}" bom LIMIT 3 HEX)
  if(bom STREQUAL "efbbbf")   # a UTF-8 byte order mark
    file(READ "${file}" text OFFSET 3)
  else()
    file(READ "${file}" text)
  endif()
  string(REPLACE "\r" "" text "${text}")
  string(STRIP "${text}" text)
  string(REPLACE "\n" "\n " text " ${text}")
  set(previous "")
  while(NOT text STREQUAL previous)
    set(previous "${text}")
    string(REGEX REPLACE "\n[ \t]*\n" "\n .\n" text "${text}")
  endwhile()
  string(REGEX REPLACE "[ \t]+\n" "\n" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

# /usr/share/doc/dr-stein/copyright (in the .deb; the same under DRSTEIN_DESKTOP_PREFIX for a
# plain install): who holds the rights to each part of the installed tree and under which licence.
# Generated from src/app/platform/copyright.in at configure time.
function(_drstein_write_copyright out)
  set(DRSTEIN_COPYRIGHT_PREFIX "${CMAKE_INSTALL_PREFIX}")
  string(REGEX REPLACE "^/+|/+$" "" DRSTEIN_COPYRIGHT_PREFIX "${DRSTEIN_COPYRIGHT_PREFIX}")
  set(DRSTEIN_QT_VERSION "${Qt6_VERSION}")
  set(DRSTEIN_QT_SERIES "${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR}")
  set(DRSTEIN_DOC_DIR "${DRSTEIN_DESKTOP_DOCDIR}")
  if(NOT IS_ABSOLUTE "${DRSTEIN_DOC_DIR}")
    set(DRSTEIN_DOC_DIR "${CMAKE_INSTALL_PREFIX}/${DRSTEIN_DOC_DIR}")
  endif()
  # The ICU that Qt's installer kits bundle in lib/ (libicuuc.so.<major>).
  set(DRSTEIN_ICU_VERSION "")
  file(GLOB icu_libs "${QT6_INSTALL_PREFIX}/lib/libicuuc.so.*")
  foreach(lib IN LISTS icu_libs)
    if(lib MATCHES "libicuuc\\.so\\.([0-9]+)$")
      set(DRSTEIN_ICU_VERSION " ${CMAKE_MATCH_1}")
    endif()
  endforeach()
  _drstein_dep5_text(DRSTEIN_LICENSE_EXPAT "${PROJECT_SOURCE_DIR}/LICENSE")
  # The program's own MIT licence: the permission text, without the title and copyright line.
  string(FIND "${DRSTEIN_LICENSE_EXPAT}" " Permission is hereby granted" at)
  if(at LESS 0)
    message(FATAL_ERROR "${PROJECT_SOURCE_DIR}/LICENSE is no longer the MIT licence this expects")
  endif()
  string(SUBSTRING "${DRSTEIN_LICENSE_EXPAT}" ${at} -1 DRSTEIN_LICENSE_EXPAT)
  _drstein_dep5_text(DRSTEIN_LICENSE_CC_BY_4 "${PROJECT_SOURCE_DIR}/src/app/icons/codicons/LICENSE")
  configure_file("${PROJECT_SOURCE_DIR}/src/app/platform/copyright.in" "${out}" @ONLY)
endfunction()

# Everything the installed <target> needs next to it: its RUNPATH, libstein's libraries, Qt
# (libraries, plugins, QML modules, qt.conf), the PATH link, the manual page and the licences.
# Call after install(TARGETS).
function(drstein_install_linux_runtime target)
  set(bindir "${CMAKE_INSTALL_BINDIR}")
  set(libdir "${DRSTEIN_INSTALL_LIBDIR}")
  file(RELATIVE_PATH bin_to_lib "/prefix/${bindir}" "/prefix/${libdir}")
  set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN/${bin_to_lib}")

  # Our own shared libraries go in by their own install rules, so that CMake swaps their
  # build-tree RUNPATH for $ORIGIN; Qt's deploy is told to leave them alone. Each is one file
  # named by its soname (VERSION = SOVERSION): only the soname is ever loaded, and a link chain
  # would be counted twice in the package's Installed-Size.
  _drstein_project_shared_libraries(own_libs ${target})
  set(deploy_args NO_TRANSLATIONS)   # the UI is English only and loads no Qt translations
  if(own_libs)
    set_target_properties(${own_libs} PROPERTIES INSTALL_RPATH "$ORIGIN")
    foreach(lib IN LISTS own_libs)
      get_target_property(soversion ${lib} SOVERSION)
      if(NOT soversion MATCHES "-NOTFOUND$" AND NOT soversion STREQUAL "")   # "0" is false in if()
        set_target_properties(${lib} PROPERTIES VERSION "${soversion}")
      endif()
    endforeach()
    install(TARGETS ${own_libs} LIBRARY DESTINATION "${libdir}" NAMELINK_SKIP)
    list(APPEND deploy_args PRE_EXCLUDE_REGEXES)
    foreach(lib IN LISTS own_libs)
      list(APPEND deploy_args "^$<TARGET_SONAME_FILE_NAME:${lib}>$")
    endforeach()
  endif()
  # Plugin types that cannot or need not load in this app: the QML debugger and profiler (needs
  # a build with QML debugging on); the helpers of the eglfs and wayland platform plugins, which
  # Qt's deploy does not ship (xcb is its one Linux platform); generic input plugins (only for
  # eglfs and linuxfb, or by -plugin); network information and TLS backends (the app makes no
  # network connections).
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
    list(APPEND deploy_args EXCLUDE_PLUGIN_TYPES qmltooling egldeviceintegrations
      wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration
      generic networkinformation tls)
  endif()
  qt6_generate_deploy_qml_app_script(TARGET ${target} OUTPUT_SCRIPT deploy_script ${deploy_args})

  # The third-party notices of the deployed Qt come from Qt's documentation of that version.
  set(docs_dir "${DRSTEIN_QT_DOCS_DIR}")
  if(NOT docs_dir)
    cmake_path(SET docs_dir NORMALIZE "${QT6_INSTALL_PREFIX}/../../Docs/Qt-${Qt6_VERSION}")
  endif()
  if(NOT EXISTS "${docs_dir}/qtcore")
    message(WARNING "Qt's documentation is not at ${docs_dir}, so the install cannot list the "
      "third-party code inside Qt (qt-third-party.txt.gz names where to find it instead). Install "
      "the documentation of Qt ${Qt6_VERSION} with Qt's maintenance tool, or set DRSTEIN_QT_DOCS_DIR.")
    set(docs_dir "")
  endif()
  set_property(GLOBAL PROPERTY DRSTEIN_QT_DOCS_FOUND "${docs_dir}")

  _drstein_quote(q_docdir "${DRSTEIN_DESKTOP_DOCDIR}")
  _drstein_quote(q_steps "${_drstein_install_steps}")
  _drstein_quote(q_stage "${CMAKE_CURRENT_BINARY_DIR}/qt-deploy-stage")
  _drstein_quote(q_bindir "${bindir}")
  _drstein_quote(q_libdir "${libdir}")
  _drstein_quote(q_style "${DRSTEIN_QT_STYLE}")
  _drstein_quote(q_strip "${CMAKE_STRIP}")
  _drstein_quote(q_python "${Python3_EXECUTABLE}")
  _drstein_quote(q_notices_tool "${PROJECT_SOURCE_DIR}/tools/qt-third-party-notices.py")
  _drstein_quote(q_docs "${docs_dir}")
  _drstein_quote(q_qt_version "${Qt6_VERSION}")
  _drstein_quote(q_notices "${CMAKE_CURRENT_BINARY_DIR}/doc/qt-third-party.txt")
  _drstein_quote(q_gzip "${DRSTEIN_GZIP}")
  install(CODE "include(${q_steps})\ndrstein_deploy_qt_begin(${q_stage} ${q_bindir} ${q_libdir})")
  install(SCRIPT "${deploy_script}")
  install(CODE "include(${q_steps})
set(_drstein_keep_style ${q_style})
set(_drstein_strip ${q_strip})
set(_drstein_python ${q_python})
set(_drstein_notices_tool ${q_notices_tool})
set(_drstein_qt_docs ${q_docs})
set(_drstein_qt_version ${q_qt_version})
set(_drstein_notices ${q_notices})
set(_drstein_gzip ${q_gzip})
set(_drstein_docdir ${q_docdir})
drstein_deploy_qt_end()")

  if(DRSTEIN_DESKTOP_BINDIR)
    _drstein_quote(q_link_dir "${DRSTEIN_DESKTOP_BINDIR}")
    _drstein_quote(q_scratch "${CMAKE_CURRENT_BINARY_DIR}/launcher-link")
    install(CODE "include(${q_steps})\ndrstein_install_launcher_link(${q_link_dir} ${q_bindir} \"$<TARGET_FILE_NAME:${target}>\" ${q_scratch})")
  endif()

  # The licences of the installed tree, and the manual page.
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doc")
  _drstein_write_copyright("${CMAKE_CURRENT_BINARY_DIR}/doc/copyright")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/doc/copyright" DESTINATION "${DRSTEIN_DESKTOP_DOCDIR}")
  set(man_page "${PROJECT_SOURCE_DIR}/src/app/platform/dr-stein.1")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${man_page}")
  execute_process(COMMAND "${DRSTEIN_GZIP}" -9 -n -c "${man_page}"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/doc/dr-stein.1.gz" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "gzip could not compress ${man_page}")
  endif()
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/doc/dr-stein.1.gz" DESTINATION "${DRSTEIN_DESKTOP_MANDIR}")
endfunction()
