# CPACK_PROJECT_CONFIG_FILE of the .deb (DrSteinPackaging.cmake): cpack reads it before it makes
# a package. tools/package-deb.sh sets DRSTEIN_PACKAGE_DEB_SH=1 for its cpack run; that script
# gives a build from git its version (+git<date>.<hash>, ~dirty<time> for uncommitted changes).
# A cpack run of its own would label any build with the release version.
# CPACK_GENERATOR is the generator cpack is running now: DEB, or a source package generator
# (package_source), which would archive the build trees in the source directory.
if(NOT CPACK_GENERATOR STREQUAL "DEB")
  message(FATAL_ERROR "Dr Stein makes only a .deb with CPack, not ${CPACK_GENERATOR}. For a "
    "source tarball use git archive (and the libstein submodule's).")
endif()
if(NOT "$ENV{DRSTEIN_PACKAGE_DEB_SH}" STREQUAL "1")
  message(FATAL_ERROR "Make the .deb with tools/package-deb.sh, which sets the package version "
    "from git: QT_ROOT=<Qt kit> tools/package-deb.sh [build-dir]")
endif()
