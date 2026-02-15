#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

ROOT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")/.."
desired_branch="${1:-main}"

if [[ "$(git -C "${ROOT_DIR}" branch --show-current)" != "main" ]]; then
    git -C "${ROOT_DIR}" fetch --prune origin main:main

    git -C "${ROOT_DIR}" checkout main
fi

git -C "${ROOT_DIR}" fetch --prune

branch=''
for branch in $(git -C "${ROOT_DIR}" branch -vv | grep ': gone]' | awk '{if ($1 == "*") print $2; else print $1}'); do
    git -C "${ROOT_DIR}" branch -D "${branch}"
done

if [[ "${desired_branch}" != 'main' ]]; then
    if git -C "${ROOT_DIR}" show-ref --verify --quiet "refs/heads/${desired_branch}"; then
        git -C "${ROOT_DIR}" checkout "${desired_branch}"
    else
        git -C "${ROOT_DIR}" checkout -b "${desired_branch}"
        git -C "${ROOT_DIR}" push --set-upstream origin "${desired_branch}"
    fi
fi
