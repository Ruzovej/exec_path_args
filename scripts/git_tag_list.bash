#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

ROOT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")/.."

git -C "${ROOT_DIR}" tag | sort --reverse # so it's sorted from e.g. `v002.000.000` to `v001.000.000` ...
