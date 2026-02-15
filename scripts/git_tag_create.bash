#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

ROOT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")/.."
desired_tag="${1:?Tag name missing!}"

if [[ -n "$(git -C "${ROOT_DIR}" status --porcelain)" ]]; then
    printf 'Working directory not clean!\n' >&2
    exit 1
fi

if [[ "$(git -C "${ROOT_DIR}" branch --show-current)" != "main" ]]; then
    printf 'Not on branch "main"!\n' >&2
    exit 1
fi

git -C "${ROOT_DIR}" fetch --tags

if [[ "$(git -C "${ROOT_DIR}" tag --list "${desired_tag}")" == "${desired_tag}" ]]; then
    printf 'Tag "%s" already exists!\n' "${desired_tag}" >&2
    exit 1
fi

git -C "${ROOT_DIR}" tag -a "${desired_tag}"

git -C "${ROOT_DIR}" push origin "${desired_tag}"
