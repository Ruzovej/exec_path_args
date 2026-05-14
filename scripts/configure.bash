#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

extra_args=()
mode="Release"

if [[ "$1" == '--debug' || "$1" == '-g' ]]; then
    mode="Debug"
    shift
elif [[ "$1" == '--asan' || "$1" == '-a' ]]; then
    mode="Debug"
    extra_args=(
        -DEXECPATHARGS_ASAN=ON
        -DEXECPATHARGS_MSAN=OFF
        -DEXECPATHARGS_UBSAN=ON
        -DEXECPATHARGS_TSAN=OFF
    )
    shift
elif [[ "$1" == '--tsan' || "$1" == '-t' ]]; then
    mode="Debug"
    extra_args=(
        -DEXECPATHARGS_ASAN=OFF
        -DEXECPATHARGS_MSAN=OFF
        -DEXECPATHARGS_UBSAN=OFF
        -DEXECPATHARGS_TSAN=ON
    )
    shift
elif [[ "$1" == '--help' || "$1" == '-h' ]]; then
    printf "Usage: %s [--debug|-g|--asan|-a|--tsan|-t] [extra cmake args]\n" "$(basename "$0")"
    exit 0
fi

time \
    cmake \
    -S . \
    -B build \
    "-DCMAKE_BUILD_TYPE=${mode}" \
    "${extra_args[@]}" \
    "$@"
