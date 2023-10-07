#!/bin/sh -eu

# This script must be erun from the top source directory.
# The purpose is to build the current source with the reasonably
# most recent compiler and strictest checks.
# Environment variables:
# BASE - which distribution to use for building
# CC, CXX compilers to use, e.g.:
#
# $ CC=clang CXX=clang++ ./scripts/docker/build.sh
#

BASE=${BASE:='debian:testing-slim'}
CC=${CC:-'gcc'}
CXX=${CXX:-'g++'}

# tag under which the docker image will be created.
TAG="galera-builder-${BASE}"

docker buildx build -t ${TAG} --build-arg base=${BASE} $(dirname $0)
docker run --volume ${PWD}:/output --env CC=${CC} --env CXX=${CXX} ${TAG}
