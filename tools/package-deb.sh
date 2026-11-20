#!/usr/bin/env bash
# Builds the Debian package of Dr Stein. Linux only; runs as yourself, no root needed.
#
#   QT_ROOT=$HOME/Qt/6.12.0/gcc_64 tools/package-deb.sh [build-dir]
#
# Configures a Release build from the "package" preset in build-dir (default: build/package in
# the source tree), builds it, runs the core tests, and has CPack make the .deb
# (cmake/DrSteinPackaging.cmake). Re-running reuses the build directory; a build directory made
# with another Qt is refused (use a new one).
#
# The package carries the Qt it was built with (in /opt/dr-stein), so any Qt 6.7+ kit from the
# Qt installer will do, with its documentation installed: the package's licence notices for the
# third-party code inside Qt are copied from it. Building also needs libfuse3-dev and pkg-config
# (mounting). Rebuild the package when Qt publishes a patch release: the bundled Qt (with its
# image decoders and ICU) gets no security updates from the distribution.
#
# Builds from a git checkout get a version suffix: +git<date>.<time>.<hash> of the commit (UTC)
# after the release tag v<version>, ~git... before it. Uncommitted changes, untracked files
# included, add ~dirty<UTC time of the build>: each such build sorts above the one before it and
# below the clean build of the same commit. A clean checkout of the tag itself gets the plain
# version. A shallow clone, or one without tags, is refused: it cannot tell after from before.
#
# Environment:
#   QT_ROOT                 the Qt kit, the directory holding bin/, lib/ and plugins/ (required)
#   CMAKE_GENERATOR         for a new build directory; default Ninja when installed, else Unix Makefiles
#   DEBFULLNAME, DEBEMAIL   the package's Maintainer field, both or neither (default: a placeholder,
#                           fine for your own machine, not for a package you hand out)
set -euo pipefail

die() {
  printf 'package-deb.sh: %s\n' "$*" >&2
  exit 1
}

src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
build=${1:-$src/build/package}
[[ $build == /* ]] || build=$PWD/$build

[[ $(uname -s) == Linux ]] || die "this makes a Debian package; run it on Linux"
[[ -n ${QT_ROOT:-} ]] || die "QT_ROOT is not set. Point it at a Qt 6.7+ kit, for example:
  QT_ROOT=\$HOME/Qt/6.12.0/gcc_64 $0 ${1:-}"
[[ -f $QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake ]] || die "QT_ROOT=$QT_ROOT is not a Qt 6 kit (there is no lib/cmake/Qt6/Qt6Config.cmake in it).
  Use the directory that holds bin/, lib/ and plugins/, for example \$HOME/Qt/6.12.0/gcc_64"
# CMake runs from the source directory: a relative QT_ROOT would point elsewhere there.
QT_ROOT=$(cd -- "$QT_ROOT" && pwd -P)
export QT_ROOT
for tool in cmake ctest cpack dpkg dpkg-shlibdeps file gzip; do
  command -v "$tool" >/dev/null || die "$tool is not installed (dpkg-shlibdeps comes with dpkg-dev; file with file)"
done
if [[ -n ${DEBFULLNAME:-} && -z ${DEBEMAIL:-} ]] || [[ -z ${DEBFULLNAME:-} && -n ${DEBEMAIL:-} ]]; then
  die "set both DEBFULLNAME and DEBEMAIL for the Maintainer field, or neither"
fi

# A build directory keeps the Qt it was configured with (Qt6_DIR in its cache) whatever QT_ROOT says.
if [[ -f $build/CMakeCache.txt ]]; then
  cached=$(sed -n 's/^Qt6_DIR:PATH=//p' "$build/CMakeCache.txt")
  if [[ -n $cached && $cached != *-NOTFOUND && $(cd -- "$cached" 2>/dev/null && pwd -P) != "$QT_ROOT/lib/cmake/Qt6" ]]; then
    die "$build was configured with the Qt in ${cached%/lib/cmake/Qt6}, not QT_ROOT=$QT_ROOT.
  Use a new build directory (or delete this one)."
  fi
fi

# The version suffix for builds from git.
version=$(sed -n 's/^project(dr_stein VERSION \([0-9.]*\).*/\1/p' "$src/CMakeLists.txt")
[[ -n $version ]] || die "could not read the project version from $src/CMakeLists.txt"
suffix=
if git -C "$src" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  dirty=
  status=$(git -C "$src" status --porcelain --untracked-files=normal) || die "git status failed in $src"
  [[ -z $status ]] || dirty="~dirty$(date -u +%Y%m%d.%H%M%S)"
  exact=$(git -C "$src" describe --tags --exact-match HEAD 2>/dev/null || true)
  if [[ $exact != "v$version" || -n $dirty ]]; then
    when=$(TZ=UTC0 git -C "$src" log -1 --date=format-local:%Y%m%d.%H%M%S --format=%cd HEAD)
    hash=$(git -C "$src" rev-parse --short=7 HEAD)
    if git -C "$src" rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
      sep='~'
      git -C "$src" merge-base --is-ancestor "v$version" HEAD && sep=+
    elif [[ $(git -C "$src" rev-parse --is-shallow-repository) == true || -z $(git -C "$src" tag -l 'v*') ]]; then
      die "this clone has no v$version tag (shallow, or cloned without tags), so the package
  version cannot tell whether the commit comes before or after release $version.
  Fetch the history and tags first: git -C $src fetch --unshallow --tags (or git fetch --tags)"
    else
      sep='~'   # v$version is not tagged yet: a build before the release
    fi
    suffix="${sep}git${when}.${hash}${dirty}"
  fi
else
  echo "package-deb.sh: $src is not a git checkout; the package version is $version"
fi

# A build directory keeps the generator it was made with.
if [[ -f $build/CMakeCache.txt ]]; then
  generator=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$build/CMakeCache.txt")
elif [[ -n ${CMAKE_GENERATOR:-} ]]; then
  generator=$CMAKE_GENERATOR
elif command -v ninja >/dev/null; then
  generator=Ninja
else
  generator="Unix Makefiles"
fi

configure=(-G "$generator" -D "DRSTEIN_PACKAGE_VERSION_SUFFIX=$suffix" -D DRSTEIN_BUILD_TESTS=ON)
if [[ -n ${DEBFULLNAME:-} ]]; then
  configure+=(-D "DRSTEIN_PACKAGE_MAINTAINER=$DEBFULLNAME <$DEBEMAIL>")
else
  configure+=(-U DRSTEIN_PACKAGE_MAINTAINER)   # back to the placeholder
fi

cd -- "$src"
cmake --preset package -B "$build" "${configure[@]}"
cmake --build "$build" --parallel "$(nproc)"
# The tests are not packaged, but a package is made only from a build that passes them.
ctest --test-dir "$build" --output-on-failure --no-tests=error

cd -- "$build"
DRSTEIN_PACKAGE_DEB_SH=1 cpack -G DEB | tee cpack.log
deb=$(sed -n 's/^CPack: - package: \(.*\) generated\.$/\1/p' cpack.log | tail -n 1)
[[ -n $deb && -f $deb ]] || die "cpack named no package; see $build/cpack.log"

printf '\nPackage: %s (%s)\n' "$deb" "$(du -h -- "$deb" | cut -f 1)"
printf 'Install it:  sudo apt install %q\n' "$deb"
printf 'Remove it:   sudo apt remove dr-stein\n'
if [[ -z ${DEBFULLNAME:-} ]]; then
  printf '\nNote: the Maintainer field is a placeholder (%s).\n' "$(dpkg-deb -f "$deb" Maintainer)"
  printf 'Set DEBFULLNAME and DEBEMAIL before making a package for anyone else.\n'
fi
