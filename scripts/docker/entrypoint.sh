#!/bin/bash -eu

# Assume source tree is rooted in output
cd /output
export CFLAGS='-Werror -Wno-deprecated-declarations'
export CMAKE_OPTS='-DGALERA_WITH_UBSAN:BOOL=ON'
export UBSAN_OPTIONS="halt_on_error=1"
./scripts/build.sh -o --cmake
