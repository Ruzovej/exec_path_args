#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

time \
    cmake \
    --build build \
    -j "$(nproc)" \
     "$@"
