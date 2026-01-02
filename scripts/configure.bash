#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

time \
    cmake \
    -S . \
    -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    "$@"
