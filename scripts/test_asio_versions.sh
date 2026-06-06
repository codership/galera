#!/usr/bin/env bash
#
# Copyright (C) 2026 MariaDB Plc <info@mariadb.com>
#
# Helper for testing that Galera builds against a range of Asio versions.
#
# For every supported Asio major.minor release the *latest patch* version is
# downloaded from GitHub and the project is configured and built against it
# using the GALERA_CUSTOM_ASIO_PATH CMake option (see cmake/asio.cmake). A
# pass/fail summary is printed at the end.
#
# The default version set is the in-tree bundled Asio copy plus the latest
# patch of every released Asio major.minor from the bundled version (1.14)
# onwards. The special version "bundled" builds against the repo's own asio/
# copy (the fallback configuration used when no suitable system Asio is
# present).
#
# Usage:
#   scripts/test_asio_versions.sh [OPTIONS] [VERSION ...]
#
# Options:
#   -j, --jobs N      parallel build jobs (default: number of CPUs)
#   -t, --tests       also run the unit tests (ctest) after each build
#   -w, --workdir D   working directory for downloads and builds
#                     (default: <repo>/build_asio)
#   -k, --keep        keep build directories on success (default: removed)
#   -x, --stop        stop at the first failing version
#   -h, --help        show this help and exit
#
# Positional VERSION arguments (e.g. bundled 1.30.2 1.34.0) override the
# default set.

set -eu

# "bundled" builds against the repo's in-tree asio/ copy (currently 1.14.1);
# the rest are the latest patch release for every released Asio major.minor,
# starting at 1.16 (1.14.1 is covered by "bundled"). 1.15.x, 1.25.x and
# 1.37.x were never released, hence the gaps.
# Update this list as new Asio releases appear.
DEFAULT_VERSIONS=(
    bundled
    1.16.1
    1.17.0
    1.18.2
    1.19.2
    1.20.0
    1.21.0
    1.22.2
    1.23.0
    1.24.0
    1.26.0
    1.27.0
    1.28.2
    1.29.0
    1.30.2
    1.31.0
    1.32.0
    1.33.0
    1.34.2
    1.35.0
    1.36.0
    1.38.0
)

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
SRC_DIR=$(cd "$SCRIPT_DIR/.." && pwd -P)

get_cores()
{
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif [ -r /proc/cpuinfo ]; then
        grep -c ^processor /proc/cpuinfo
    else
        echo 1
    fi
}

usage()
{
    # Print the leading comment block (skipping the shebang and copyright),
    # stripping the leading "# ".
    awk 'NR<=4 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' \
        "${BASH_SOURCE[0]}"
}

JOBS=$(get_cores)
RUN_TESTS="no"
WORKDIR="$SRC_DIR/build_asio"
KEEP="no"
STOP_ON_FAIL="no"
VERSIONS=()

while [ $# -gt 0 ]; do
    case "$1" in
        -j|--jobs)    JOBS="$2"; shift 2 ;;
        -t|--tests)   RUN_TESTS="yes"; shift ;;
        -w|--workdir) WORKDIR="$2"; shift 2 ;;
        -k|--keep)    KEEP="yes"; shift ;;
        -x|--stop)    STOP_ON_FAIL="yes"; shift ;;
        -h|--help)    usage; exit 0 ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
        *)            VERSIONS+=("$1"); shift ;;
    esac
done

if [ ${#VERSIONS[@]} -eq 0 ]; then
    VERSIONS=("${DEFAULT_VERSIONS[@]}")
fi

# Pick a download command.
if command -v curl >/dev/null 2>&1; then
    download() { curl -fsSL "$1" -o "$2"; }
elif command -v wget >/dev/null 2>&1; then
    download() { wget -q "$1" -O "$2"; }
else
    echo "Error: neither curl nor wget found" >&2
    exit 1
fi

mkdir -p "$WORKDIR"

# Download and extract a given Asio version, echo the include directory to use
# as GALERA_CUSTOM_ASIO_PATH. Cached: re-extracted only if missing.
fetch_asio()
{
    local ver="$1"
    local tag="asio-${ver//./-}"
    local tarball="$WORKDIR/${tag}.tar.gz"
    local extract_dir="$WORKDIR/$tag"
    local inc_dir="$extract_dir/asio-${tag}/asio/include"

    if [ ! -d "$inc_dir" ]; then
        if [ ! -s "$tarball" ]; then
            download \
                "https://github.com/chriskohlhoff/asio/archive/refs/tags/${tag}.tar.gz" \
                "$tarball"
        fi
        mkdir -p "$extract_dir"
        tar xzf "$tarball" -C "$extract_dir"
    fi

    if [ ! -f "$inc_dir/asio.hpp" ]; then
        echo "Error: asio.hpp not found under $inc_dir" >&2
        return 1
    fi
    echo "$inc_dir"
}

# Configure and build the project against a single Asio version.
build_one()
{
    local ver="$1"
    local inc_dir build_dir log

    if [ "$ver" = "bundled" ]; then
        # Build against the in-tree Asio copy shipped in the repo, i.e. the
        # fallback configuration used when no suitable system Asio is found.
        inc_dir="$SRC_DIR/asio"
    else
        inc_dir=$(fetch_asio "$ver") || return 1
    fi

    build_dir="$WORKDIR/build-$ver"
    log="$WORKDIR/build-$ver.log"
    rm -rf "$build_dir"

    {
        echo "=== Asio $ver: configure ==="
        cmake -S "$SRC_DIR" -B "$build_dir" \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DGALERA_CUSTOM_ASIO_PATH="$inc_dir" || return 1

        echo "=== Asio $ver: build ==="
        cmake --build "$build_dir" -j "$JOBS" || return 1

        if [ "$RUN_TESTS" = "yes" ]; then
            echo "=== Asio $ver: ctest ==="
            ctest --test-dir "$build_dir" --output-on-failure || return 1
        fi
    } >"$log" 2>&1

    [ "$KEEP" = "yes" ] || rm -rf "$build_dir"
}

echo "Galera source : $SRC_DIR"
echo "Work directory: $WORKDIR"
echo "Parallel jobs : $JOBS"
echo "Run tests     : $RUN_TESTS"
echo "Versions      : ${VERSIONS[*]}"
echo

declare -a RESULTS
overall_rc=0

for ver in "${VERSIONS[@]}"; do
    printf '>>> Building against Asio %s ... ' "$ver"
    if build_one "$ver"; then
        echo "PASS"
        RESULTS+=("PASS  $ver")
    else
        echo "FAIL (see $WORKDIR/build-$ver.log)"
        RESULTS+=("FAIL  $ver")
        overall_rc=1
        if [ "$STOP_ON_FAIL" = "yes" ]; then
            break
        fi
    fi
done

echo
echo "==================== Summary ===================="
for r in "${RESULTS[@]}"; do
    echo "  $r"
done
echo "================================================="

exit $overall_rc
