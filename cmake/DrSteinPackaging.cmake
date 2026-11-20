# The Debian package (Linux): CPack's DEB generator over the install rules, so the .deb holds the
# tree `cmake --install` makes (DrSteinLinuxInstall.cmake): /opt/dr-stein with its own Qt, and
# /usr/bin/dr-stein, the .desktop file, the icons, the manual page and /usr/share/doc/dr-stein
# under /usr. The top-level CMakeLists.txt includes this only for a build configured with
# DRSTEIN_DESKTOP_PREFIX=/usr (the "package" preset): no other build has a `package` target.
# tools/package-deb.sh drives it, and cpack refuses to run without it (DrSteinCPackProject.cmake),
# because the script is what gives a build from git its version:
#
#   QT_ROOT=$HOME/Qt/6.12.0/gcc_64 tools/package-deb.sh
#
# Depends is computed by dpkg-shlibdeps from every ELF file in the package; the bundled libraries
# in /opt/dr-stein/lib count as the package's own. The icon cache and the desktop database are
# refreshed by dpkg triggers (hicolor-icon-theme, desktop-file-utils), so there are no
# maintainer scripts.
include_guard(GLOBAL)

set(DRSTEIN_PACKAGE_VERSION_SUFFIX "" CACHE STRING
  "Appended to the project version in the package version, e.g. +git20261119.212341.becf8a3 (tools/package-deb.sh sets it for builds from git)")
set(DRSTEIN_PACKAGE_MAINTAINER "Dr Stein packager <packager@localhost>" CACHE STRING
  "The package's Maintainer field, \"Name <email>\" (tools/package-deb.sh takes DEBFULLNAME and DEBEMAIL)")
if(NOT DRSTEIN_PACKAGE_VERSION_SUFFIX MATCHES "^[A-Za-z0-9.+~]*$")
  message(FATAL_ERROR "DRSTEIN_PACKAGE_VERSION_SUFFIX may hold only letters, digits, '.', '+' and '~' "
    "(Debian version syntax), not \"${DRSTEIN_PACKAGE_VERSION_SUFFIX}\"")
endif()

# A package without libstein's FUSE backend could not mount anything, yet would recommend fuse3.
# libstein builds without it when libfuse3's development files are missing.
get_target_property(_drstein_mount_defs stein_mount INTERFACE_COMPILE_DEFINITIONS)
if(NOT "STEIN_HAVE_FUSE=1" IN_LIST _drstein_mount_defs)
  message(FATAL_ERROR "libstein was configured without its FUSE mount backend, so the package "
    "could not mount images. Install libfuse3's development files and pkg-config "
    "(sudo apt install libfuse3-dev pkg-config) and configure again in a fresh build directory.")
endif()
# The third-party notices of the bundled Qt come from Qt's documentation (DrSteinLinuxInstall.cmake).
get_property(_drstein_qt_docs GLOBAL PROPERTY DRSTEIN_QT_DOCS_FOUND)
if(NOT _drstein_qt_docs)
  message(FATAL_ERROR "The package carries Qt, and with it the licence notices of the third-party "
    "code inside Qt, which are copied from Qt's documentation; it was not found (see the warning "
    "above). Install the documentation of Qt ${Qt6_VERSION} with Qt's maintenance tool, or set "
    "DRSTEIN_QT_DOCS_DIR to it.")
endif()

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME dr-stein)
set(CPACK_PACKAGE_VENDOR "Dr Stein")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}${DRSTEIN_PACKAGE_VERSION_SUFFIX}")
set(CPACK_PACKAGE_CONTACT "${DRSTEIN_PACKAGE_MAINTAINER}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/streamx3/dr_stein")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "disk and disk-image multitool")
# Hard-wrapped: the control file keeps these lines as they are.
set(CPACK_PACKAGE_DESCRIPTION
  "Dr Stein shows how a disk or a disk image is laid out (partition tables,
LUKS containers, LVM volumes, file systems), browses and previews the files
inside without mounting them, images disks and partitions and restores
them, edits and repairs partition tables, and runs surface scans and
fake-flash capacity tests. Everything opens read-only; writes need an
explicit confirmation.

It opens raw and split raw images, qcow2, VHD, VHDX, VMDK, VDI, E01, DMG
and its own .stein images. Real disks need administrator rights, which the
app asks for through pkexec.

The package carries the Qt libraries it was built with in /opt/dr-stein
and does not use the distribution's Qt.")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_STRIP_FILES ON)

# DEB installs through DESTDIR on its own, with this as the prefix, which is what lets the
# absolute /usr destinations of the desktop files land in the package as well.
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/dr-stein")

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)            # dr-stein_<version>-1_<arch>.deb
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${DRSTEIN_PACKAGE_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION admin)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CPACK_PACKAGING_INSTALL_PREFIX}/${DRSTEIN_INSTALL_LIBDIR}")
# pkexec: the in-app relaunch with administrator rights. fuse3: fusermount3, which libfuse3 runs
# to mount for a user without root (the read-only mounts); as root it mounts by itself.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pkexec, fuse3")
set(CPACK_DEBIAN_COMPRESSION_TYPE xz)

# Read by cpack before it packages: refuses runs not started by tools/package-deb.sh and source
# packages (which would archive the build trees in the source directory).
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/DrSteinCPackProject.cmake")
set(CPACK_SOURCE_IGNORE_FILES "/\\.git/" "/build/" "\\.deb$")

# /usr/share/doc/dr-stein/changelog.Debian.gz: Debian packages carry one. The copyright file next
# to it comes from DrSteinLinuxInstall.cmake.
string(TIMESTAMP _drstein_changelog_date "%a, %d %b %Y %H:%M:%S +0000" UTC)
file(WRITE "${CMAKE_BINARY_DIR}/doc/changelog.Debian"
  "dr-stein (${CPACK_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}) unstable; urgency=medium\n\n"
  "  * Package built by tools/package-deb.sh. The changes are in the\n"
  "    project's git history: ${CPACK_PACKAGE_HOMEPAGE_URL}\n\n"
  " -- ${DRSTEIN_PACKAGE_MAINTAINER}  ${_drstein_changelog_date}\n")
execute_process(COMMAND "${DRSTEIN_GZIP}" -9 -n -c "${CMAKE_BINARY_DIR}/doc/changelog.Debian"
  OUTPUT_FILE "${CMAKE_BINARY_DIR}/doc/changelog.Debian.gz" RESULT_VARIABLE _drstein_rc)
if(NOT _drstein_rc EQUAL 0)
  message(FATAL_ERROR "gzip could not compress ${CMAKE_BINARY_DIR}/doc/changelog.Debian")
endif()
install(FILES "${CMAKE_BINARY_DIR}/doc/changelog.Debian.gz" DESTINATION "${DRSTEIN_DESKTOP_DOCDIR}")

include(CPack)
