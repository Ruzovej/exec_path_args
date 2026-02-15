#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

ROOT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")/.."

if [[ -z "$1" ]]; then
    git -C "${ROOT_DIR}" tag --sort=-v:refname
else
    git -C "${ROOT_DIR}" tag --list "$1"
fi
